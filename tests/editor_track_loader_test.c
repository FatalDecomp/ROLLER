#include "editor_track_loader.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char g_szChunkRecord0[] =
    "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\r\n";
static const char g_szChunkRecord1[] =
    "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\r\n";
static const char g_szChunkRecord2[] =
    "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\r\n";

static void write_u32_le(FILE *pFile, uint32_t uiValue)
{
    uint8_t abyValue[4] = {
        (uint8_t)(uiValue & 0xffu),
        (uint8_t)((uiValue >> 8) & 0xffu),
        (uint8_t)((uiValue >> 16) & 0xffu),
        (uint8_t)(uiValue >> 24)
    };
    assert(fwrite(abyValue, 1, sizeof(abyValue), pFile)
           == sizeof(abyValue));
}

static void write_literal_compacted(const char *szPath, const char *szText)
{
    FILE *pFile = fopen(szPath, "wb");
    size_t uiLength = strlen(szText);
    size_t uiOffset = 0;

    assert(pFile != NULL);
    assert(uiLength <= UINT32_MAX);
    write_u32_le(pFile, (uint32_t)uiLength);
    while (uiOffset < uiLength) {
        size_t uiBlockLength = uiLength - uiOffset;
        uint8_t byCommand;

        if (uiBlockLength > 63u)
            uiBlockLength = 63u;
        byCommand = (uint8_t)uiBlockLength;
        assert(fwrite(&byCommand, 1, 1, pFile) == 1);
        assert(fwrite(szText + uiOffset, 1, uiBlockLength, pFile)
               == uiBlockLength);
        uiOffset += uiBlockLength;
    }
    {
        const uint8_t byEnd = 0;
        assert(fwrite(&byEnd, 1, 1, pFile) == 1);
    }
    assert(fclose(pFile) == 0);
}

static void write_bytes(const char *szPath,
                        uint32_t uiDeclaredLength,
                        const uint8_t *pbyBytes,
                        size_t uiLength)
{
    FILE *pFile = fopen(szPath, "wb");

    assert(pFile != NULL);
    write_u32_le(pFile, uiDeclaredLength);
    assert(fwrite(pbyBytes, 1, uiLength, pFile) == uiLength);
    assert(fclose(pFile) == 0);
}

static char *make_track_text(const char *szHeader,
                             const char *szTextureFile)
{
    const char *szSuffixFormat =
        "T:%s\r\n"
        "BLD:BUILDING.DRH\r\n"
        "BACKS:\r\n"
        "-1 -1\r\n";
    size_t uiCapacity = strlen(szHeader)
                      + strlen(g_szChunkRecord0)
                      + strlen(g_szChunkRecord1)
                      + strlen(g_szChunkRecord2)
                      + strlen(szSuffixFormat)
                      + strlen(szTextureFile)
                      + 1u;
    char *szText = malloc(uiCapacity);

    assert(szText != NULL);
    snprintf(szText, uiCapacity, "%s%s%s%sT:%s\r\n"
             "BLD:BUILDING.DRH\r\nBACKS:\r\n-1 -1\r\n",
             szHeader,
             g_szChunkRecord0,
             g_szChunkRecord1,
             g_szChunkRecord2,
             szTextureFile);
    return szText;
}

static void join_path(char *szPath,
                      size_t uiCapacity,
                      const char *szDirectory,
                      const char *szFilename)
{
    size_t uiLength = strlen(szDirectory);
    const char *szSeparator =
        uiLength > 0
        && (szDirectory[uiLength - 1u] == '/'
            || szDirectory[uiLength - 1u] == '\\')
        ? ""
        : "/";
    int iWritten = snprintf(
        szPath, uiCapacity, "%s%s%s",
        szDirectory, szSeparator, szFilename);
    assert(iWritten >= 0 && (size_t)iWritten < uiCapacity);
}

static void test_real_track(const char *szRealTrack)
{
    tEdTrackStage Stage;
    char szError[256];
    eEdTrackLoadResult eResult;

    ed_track_stage_init(&Stage);
    eResult = ed_track_file_stage(
        szRealTrack, &Stage, szError, sizeof(szError));
    if (eResult != ED_TRACK_LOAD_OK)
        fprintf(stderr, "real track stage failed: %s (%s)\n",
                ed_track_load_result_name(eResult), szError);
    assert(eResult == ED_TRACK_LOAD_OK);
    assert(Stage.uiChunkCount > 0);
    assert(Stage.uiChunkCount <= ED_TRACK_MAX_CHUNKS);
    assert(Stage.uiDataLength > 1000u);
    assert(Stage.szTextureFile[0] != '\0');
    assert(Stage.szBuildingTextureFile[0] != '\0');
    ed_track_stage_dispose(&Stage);
}

static void test_decoder_rejects_unsafe_streams(
    const char *szBackReference,
    const char *szOverflow,
    const char *szTruncated)
{
    static const uint8_t abyBackReference[] = { 0x80u, 0u };
    static const uint8_t abyOverflow[] = { 3u, 'a', 'b', 'c', 0u };
    static const uint8_t abyTruncated[] = { 4u, 'a', 'b' };
    tEdTrackStage Stage;
    char szError[256];

    write_bytes(szBackReference, 3u, abyBackReference,
                sizeof(abyBackReference));
    write_bytes(szOverflow, 2u, abyOverflow, sizeof(abyOverflow));
    write_bytes(szTruncated, 4u, abyTruncated, sizeof(abyTruncated));

    ed_track_stage_init(&Stage);
    assert(ed_compacted_file_stage(
        szBackReference, &Stage, szError, sizeof(szError))
        == ED_TRACK_LOAD_INVALID_BACK_REFERENCE);
    assert(ed_compacted_file_stage(
        szOverflow, &Stage, szError, sizeof(szError))
        == ED_TRACK_LOAD_OUTPUT_OVERFLOW);
    assert(ed_compacted_file_stage(
        szTruncated, &Stage, szError, sizeof(szError))
        == ED_TRACK_LOAD_TRUNCATED);
    ed_track_stage_dispose(&Stage);
}

static void test_transactional_reload_soak(const char *szValidA,
                                           const char *szValidB,
                                           const char *szMalformed)
{
    tEdTrackDocument Document;
    char szError[256];

    ed_track_document_init(&Document);
    assert(ed_track_document_reload(
        &Document, szValidA, szError, sizeof(szError))
        == ED_TRACK_LOAD_OK);
    assert(Document.uiGeneration == 1u);

    for (uint32_t iCycle = 0; iCycle < 1000u; iCycle++) {
        uint8_t *pbyCommitted = Document.Committed.pbyData;
        uint64_t ullCommittedHash = Document.Committed.ullContentHash;
        uint32_t uiGeneration = Document.uiGeneration;

        assert(ed_track_document_reload(
            &Document, szMalformed, szError, sizeof(szError))
            == ED_TRACK_LOAD_MALFORMED_TEXT);
        assert(Document.Committed.pbyData == pbyCommitted);
        assert(Document.Committed.ullContentHash == ullCommittedHash);
        assert(Document.uiGeneration == uiGeneration);

        assert(ed_track_document_reload(
            &Document, szValidB, szError, sizeof(szError))
            == ED_TRACK_LOAD_OK);
        assert(Document.uiGeneration == uiGeneration + 1u);
        assert(Document.Committed.ullContentHash != ullCommittedHash);

        pbyCommitted = Document.Committed.pbyData;
        ullCommittedHash = Document.Committed.ullContentHash;
        uiGeneration = Document.uiGeneration;
        assert(ed_track_document_reload(
            &Document, szMalformed, szError, sizeof(szError))
            == ED_TRACK_LOAD_MALFORMED_TEXT);
        assert(Document.Committed.pbyData == pbyCommitted);
        assert(Document.Committed.ullContentHash == ullCommittedHash);
        assert(Document.uiGeneration == uiGeneration);

        assert(ed_track_document_reload(
            &Document, szValidA, szError, sizeof(szError))
            == ED_TRACK_LOAD_OK);
        assert(Document.uiGeneration == uiGeneration + 1u);
    }

    ed_track_document_dispose(&Document);
}

int main(int argc, char **argv)
{
    char szValidA[1024];
    char szValidB[1024];
    char szMalformed[1024];
    char szBackReference[1024];
    char szOverflow[1024];
    char szTruncated[1024];
    char *szTextA;
    char *szTextB;
    char *szMalformedText;

    assert(argc == 3);
    join_path(szValidA, sizeof(szValidA), argv[2], "f_s3_valid_a.trk");
    join_path(szValidB, sizeof(szValidB), argv[2], "f_s3_valid_b.trk");
    join_path(szMalformed, sizeof(szMalformed), argv[2],
              "f_s3_malformed_text.trk");
    join_path(szBackReference, sizeof(szBackReference), argv[2],
              "f_s3_bad_backref.trk");
    join_path(szOverflow, sizeof(szOverflow), argv[2],
              "f_s3_output_overflow.trk");
    join_path(szTruncated, sizeof(szTruncated), argv[2],
              "f_s3_truncated.trk");

    szTextA = make_track_text("  1 0 0 0\r\n", "TRACK1.DRH");
    szTextB = make_track_text("  1 1 0 0\r\n", "TRACK1.DRH");
    szMalformedText = make_track_text(
        "  1 0 0 0\r\n", "TRACK1.DRH");
    {
        char *szRecord = strstr(szMalformedText, g_szChunkRecord1);
        assert(szRecord != NULL);
        szRecord[0] = 'X';
    }
    write_literal_compacted(szValidA, szTextA);
    write_literal_compacted(szValidB, szTextB);
    write_literal_compacted(szMalformed, szMalformedText);
    free(szTextA);
    free(szTextB);
    free(szMalformedText);

    test_real_track(argv[1]);
    test_decoder_rejects_unsafe_streams(
        szBackReference, szOverflow, szTruncated);
    test_transactional_reload_soak(szValidA, szValidB, szMalformed);

    remove(szValidA);
    remove(szValidB);
    remove(szMalformed);
    remove(szBackReference);
    remove(szOverflow);
    remove(szTruncated);
    assert(ed_track_loader_live_allocations() == 0u);
    assert(ed_track_loader_live_bytes() == 0u);
    puts("editor track staged reload soak passed");
    return 0;
}
