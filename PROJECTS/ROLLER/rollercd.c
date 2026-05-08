#include "rollercd.h"
#include "roller.h"
#include "func2.h"
#include "types.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <SDL3/SDL.h>
#include <sys/stat.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/disc.h>
#include <cdio/cd_types.h>
#ifdef IS_WINDOWS
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
//-------------------------------------------------------------------------------------------------
#else
#include <unistd.h>
#endif
//-------------------------------------------------------------------------------------------------

typedef enum
{
  CUE_TRACK_UNKNOWN,
  CUE_TRACK_AUDIO,
  CUE_TRACK_DATA
} tCueTrackKind;

typedef struct
{
  char szFile[ROLLER_MAX_PATH];
  char szMode[32];
  track_t uiTrack;
  tCueTrackKind eKind;
  bool bBinaryFile;
  bool bHasFile;
  bool bHasIndex01;
  int iIndex01Sector;
} tCueTrackInfo;

typedef struct
{
  tCueTrackInfo tracks[ROLLER_MAX_CUE_TRACKS];
  int iNumTracks;
  int iNumFiles;
  bool bMultiFile;
} tCueInfo;

//-------------------------------------------------------------------------------------------------

static void WriteU16LE(FILE *pOut, uint16 v)
{
  uint8 b[2] = { (uint8)(v & 0xff), (uint8)((v >> 8) & 0xff) };
  fwrite(b, 1, 2, pOut);
}

//-------------------------------------------------------------------------------------------------

static void WriteU32LE(FILE *pOut, uint32 v)
{
  uint8 b[4] = { (uint8)(v & 0xff), (uint8)((v >> 8) & 0xff),
                 (uint8)((v >> 16) & 0xff), (uint8)((v >> 24) & 0xff) };
  fwrite(b, 1, 4, pOut);
}

//-------------------------------------------------------------------------------------------------

// Write a 44-byte RIFF PCM WAV header for CD-DA (44100 Hz, stereo, 16-bit).
static void WriteWAVHeader(FILE *pOut, uint32 uiDataBytes)
{
  fwrite("RIFF", 1, 4, pOut);
  WriteU32LE(pOut, 36 + uiDataBytes); // file size minus 8
  fwrite("WAVE", 1, 4, pOut);
  fwrite("fmt ", 1, 4, pOut);
  WriteU32LE(pOut, 16);      // fmt chunk size
  WriteU16LE(pOut, 1);       // PCM
  WriteU16LE(pOut, 2);       // stereo
  WriteU32LE(pOut, 44100);   // sample rate
  WriteU32LE(pOut, 176400);  // byte rate: 44100 * 2 * 2
  WriteU16LE(pOut, 4);       // block align: 2 channels * 2 bytes
  WriteU16LE(pOut, 16);      // bits per sample
  fwrite("data", 1, 4, pOut);
  WriteU32LE(pOut, uiDataBytes);
}

//-------------------------------------------------------------------------------------------------

static int CueCharLower(int c)
{
  return tolower((unsigned char)c);
}

//-------------------------------------------------------------------------------------------------

static bool CueStringEqualsIgnoreCase(const char *szA, const char *szB)
{
  while (*szA && *szB) {
    if (CueCharLower(*szA) != CueCharLower(*szB))
      return false;
    szA++;
    szB++;
  }
  return *szA == '\0' && *szB == '\0';
}

//-------------------------------------------------------------------------------------------------

static bool CueStringEndsWithIgnoreCase(const char *szText, const char *szSuffix)
{
  size_t nTextLen = strlen(szText);
  size_t nSuffixLen = strlen(szSuffix);
  if (nSuffixLen > nTextLen)
    return false;
  return CueStringEqualsIgnoreCase(szText + nTextLen - nSuffixLen, szSuffix);
}

//-------------------------------------------------------------------------------------------------

static void NormalizePathForCdio(const char *szPath, char *szNormPath, int nBufSize)
{
  SDL_strlcpy(szNormPath, szPath, nBufSize);
  for (char *p = szNormPath; *p; p++) {
    if (*p == '\\') *p = '/';
  }
}

//-------------------------------------------------------------------------------------------------

static void GetPathDirectory(const char *szPath, char *szDir, int nBufSize)
{
  SDL_strlcpy(szDir, szPath, nBufSize);

  char *szSlash = SDL_strrchr(szDir, '/');
  char *szBackslash = SDL_strrchr(szDir, '\\');
  if (szBackslash && (!szSlash || szBackslash > szSlash))
    szSlash = szBackslash;

  if (szSlash) {
    *(szSlash + 1) = '\0';
  } else {
    szDir[0] = '\0';
  }
}

//-------------------------------------------------------------------------------------------------

static const char *GetPathFilename(const char *szPath)
{
  const char *szSlash = SDL_strrchr(szPath, '/');
  const char *szBackslash = SDL_strrchr(szPath, '\\');
  if (szBackslash && (!szSlash || szBackslash > szSlash))
    szSlash = szBackslash;
  return szSlash ? szSlash + 1 : szPath;
}

//-------------------------------------------------------------------------------------------------

static bool RawFileExists(const char *szPath)
{
  FILE *pFile = fopen(szPath, "rb");
  if (!pFile)
    return false;
  fclose(pFile);
  return true;
}

//-------------------------------------------------------------------------------------------------

static bool BuildTempCuePath(const char *szDir, char *szCuePath, int nBufSize)
{
  for (int i = 0; i < 100; i++) {
    int iLen = SDL_snprintf(szCuePath, nBufSize, "%sroller_data_track_%02d.cue", szDir, i);
    if (iLen < 0 || iLen >= nBufSize)
      return false;
    if (!RawFileExists(szCuePath))
      return true;
  }

  SDL_Log("BuildTempCuePath: could not find an unused temporary cue filename");
  return false;
}

//-------------------------------------------------------------------------------------------------

static bool IsAbsolutePath(const char *szPath)
{
  if (!szPath || !szPath[0])
    return false;

  if (szPath[0] == '/' || szPath[0] == '\\')
    return true;

  return isalpha((unsigned char)szPath[0]) && szPath[1] == ':';
}

//-------------------------------------------------------------------------------------------------

static int ChangeWorkingDirectory(const char *szDir)
{
#ifdef IS_WINDOWS
  if (isalpha((unsigned char)szDir[0]) && szDir[1] == ':') {
    char cDriveLetter = szDir[0] & 0xDF;
    _chdrive((int)(cDriveLetter - 'A' + 1));
  }
#endif
  return chdir(szDir);
}

//-------------------------------------------------------------------------------------------------

void SaveDefaultFatalIni(const char *szWhipRoot)
{
  char szPreviousDir[ROLLER_MAX_PATH];

  if (!getcwd(szPreviousDir, sizeof(szPreviousDir))) {
    SDL_Log("ExtractFATDATA: failed to capture current directory before saving FATAL.INI: %s", strerror(errno));
    return;
  }

  setdirectory(szWhipRoot);
  save_fatal_config();

  if (ChangeWorkingDirectory(szPreviousDir) != 0)
    SDL_Log("ExtractFATDATA: failed to restore directory '%s': %s", szPreviousDir, strerror(errno));
}

//-------------------------------------------------------------------------------------------------

static void JoinCuePath(const char *szCueDir, const char *szFile, char *szOut, int nBufSize)
{
  if (IsAbsolutePath(szFile)) {
    SDL_strlcpy(szOut, szFile, nBufSize);
  } else {
    SDL_snprintf(szOut, nBufSize, "%s%s", szCueDir, szFile);
  }
}

//-------------------------------------------------------------------------------------------------

static bool ParseCueFileLine(const char *szLine, char *szFile, int nFileSize, char *szType, int nTypeSize)
{
  char szParsedFile[ROLLER_MAX_PATH];
  char szParsedType[32];

  if (sscanf(szLine, " FILE \"%259[^\"]\" %31s", szParsedFile, szParsedType) == 2) {
    SDL_strlcpy(szFile, szParsedFile, nFileSize);
    SDL_strlcpy(szType, szParsedType, nTypeSize);
    return true;
  }

  if (sscanf(szLine, " FILE %259s %31s", szParsedFile, szParsedType) == 2) {
    SDL_strlcpy(szFile, szParsedFile, nFileSize);
    SDL_strlcpy(szType, szParsedType, nTypeSize);
    return true;
  }

  return false;
}

//-------------------------------------------------------------------------------------------------

static bool ParseCueTimeToSector(const char *szTime, int *piSector)
{
  int iMinute;
  int iSecond;
  int iFrame;

  if (sscanf(szTime, "%d:%d:%d", &iMinute, &iSecond, &iFrame) != 3)
    return false;

  if (iMinute < 0 || iSecond < 0 || iSecond >= 60 || iFrame < 0 || iFrame >= 75)
    return false;

  *piSector = ((iMinute * 60) + iSecond) * 75 + iFrame;
  return true;
}

//-------------------------------------------------------------------------------------------------

static bool ParseCueFile(const char *szCuePath, tCueInfo *pCueInfo)
{
  FILE *pCue = fopen(szCuePath, "r");
  if (!pCue)
    return false;

  memset(pCueInfo, 0, sizeof(*pCueInfo));

  char szCueDir[ROLLER_MAX_PATH];
  char szCurrentFile[ROLLER_MAX_PATH] = { 0 };
  char szFirstFile[ROLLER_MAX_PATH] = { 0 };
  bool bCurrentFileIsBinary = true;
  tCueTrackInfo *pCurrentTrack = NULL;

  GetPathDirectory(szCuePath, szCueDir, ROLLER_MAX_PATH);

  char szLine[4096];
  while (fgets(szLine, sizeof(szLine), pCue)) {
    char szFile[ROLLER_MAX_PATH];
    char szType[32];

    if (ParseCueFileLine(szLine, szFile, sizeof(szFile), szType, sizeof(szType))) {
      char szResolvedFile[ROLLER_MAX_PATH];
      JoinCuePath(szCueDir, szFile, szResolvedFile, sizeof(szResolvedFile));
      SDL_strlcpy(szCurrentFile, szResolvedFile, sizeof(szCurrentFile));
      bCurrentFileIsBinary = CueStringEqualsIgnoreCase(szType, "BINARY");

      if (szFirstFile[0] == '\0') {
        SDL_strlcpy(szFirstFile, szCurrentFile, sizeof(szFirstFile));
      } else if (!CueStringEqualsIgnoreCase(szFirstFile, szCurrentFile)) {
        pCueInfo->bMultiFile = true;
      }

      pCueInfo->iNumFiles++;
      continue;
    }

    unsigned uiTrack;
    char szMode[32];
    if (sscanf(szLine, " TRACK %u %31s", &uiTrack, szMode) == 2) {
      if (pCueInfo->iNumTracks >= ROLLER_MAX_CUE_TRACKS) {
        SDL_Log("ParseCueFile: too many tracks in '%s'", szCuePath);
        fclose(pCue);
        return false;
      }

      pCurrentTrack = &pCueInfo->tracks[pCueInfo->iNumTracks++];
      memset(pCurrentTrack, 0, sizeof(*pCurrentTrack));
      pCurrentTrack->uiTrack = (track_t)uiTrack;
      pCurrentTrack->bBinaryFile = bCurrentFileIsBinary;
      pCurrentTrack->bHasFile = szCurrentFile[0] != '\0';
      pCurrentTrack->iIndex01Sector = 0;
      SDL_strlcpy(pCurrentTrack->szFile, szCurrentFile, sizeof(pCurrentTrack->szFile));
      SDL_strlcpy(pCurrentTrack->szMode, szMode, sizeof(pCurrentTrack->szMode));

      pCurrentTrack->eKind = CueStringEqualsIgnoreCase(szMode, "AUDIO")
        ? CUE_TRACK_AUDIO
        : CUE_TRACK_DATA;
      continue;
    }

    unsigned uiIndex;
    char szTime[32];
    if (pCurrentTrack && sscanf(szLine, " INDEX %u %31s", &uiIndex, szTime) == 2) {
      if (uiIndex == 1 && ParseCueTimeToSector(szTime, &pCurrentTrack->iIndex01Sector))
        pCurrentTrack->bHasIndex01 = true;
    }
  }

  fclose(pCue);
  return pCueInfo->iNumTracks > 0;
}

//-------------------------------------------------------------------------------------------------

static const tCueTrackInfo *FindCueDataTrack(const tCueInfo *pCueInfo)
{
  for (int i = 0; i < pCueInfo->iNumTracks; i++) {
    const tCueTrackInfo *pTrack = &pCueInfo->tracks[i];
    if (pTrack->eKind == CUE_TRACK_DATA && pTrack->bHasFile)
      return pTrack;
  }
  return NULL;
}

//-------------------------------------------------------------------------------------------------

static bool GetOpenFileSize(FILE *pFile, uint64 *puiSize)
{
#ifdef IS_WINDOWS
  if (_fseeki64(pFile, 0, SEEK_END) != 0)
    return false;
  __int64 iSize = _ftelli64(pFile);
  if (iSize < 0)
    return false;
  if (_fseeki64(pFile, 0, SEEK_SET) != 0)
    return false;
  *puiSize = (uint64)iSize;
#else
  if (fseeko(pFile, 0, SEEK_END) != 0)
    return false;
  off_t iSize = ftello(pFile);
  if (iSize < 0)
    return false;
  if (fseeko(pFile, 0, SEEK_SET) != 0)
    return false;
  *puiSize = (uint64)iSize;
#endif
  return true;
}

//-------------------------------------------------------------------------------------------------

static bool SeekOpenFile(FILE *pFile, uint64 uiOffset)
{
#ifdef IS_WINDOWS
  return _fseeki64(pFile, (__int64)uiOffset, SEEK_SET) == 0;
#else
  return fseeko(pFile, (off_t)uiOffset, SEEK_SET) == 0;
#endif
}

//-------------------------------------------------------------------------------------------------
// Extract CD-DA audio tracks 2..N from an already-opened CdIo image.
// Writes track02.wav .. trackNN.wav into szOutDir/audio/.
void ExtractAudioTracks(CdIo_t *p_cdio, const char *szOutDir)
{
  char szAudioDir[ROLLER_MAX_PATH];
  SDL_snprintf(szAudioDir, ROLLER_MAX_PATH, "%s/audio", szOutDir);
  SDL_CreateDirectory(szAudioDir);

  track_t uiFirst = cdio_get_first_track_num(p_cdio);
  track_t uiLast  = cdio_get_last_track_num(p_cdio);
  SDL_Log("ExtractAudioTracks: tracks %u-%u", (unsigned)uiFirst, (unsigned)uiLast);

  // Track 1 is the data track; audio starts at track 2.
  for (track_t t = 2; t <= uiLast; t++) {
    if (cdio_get_track_format(p_cdio, t) != TRACK_FORMAT_AUDIO) {
      SDL_Log("ExtractAudioTracks: track %u is not audio, skipping", (unsigned)t);
      continue;
    }

    lsn_t lsnStart = cdio_get_track_lsn(p_cdio, t);
    lsn_t lsnLast  = cdio_get_track_last_lsn(p_cdio, t);
    if (lsnStart == CDIO_INVALID_LSN || lsnLast == CDIO_INVALID_LSN) {
      SDL_Log("ExtractAudioTracks: track %u has invalid LSN, skipping", (unsigned)t);
      continue;
    }

    uint32 uiSectors   = (uint32)(lsnLast - lsnStart + 1);
    uint32 uiDataBytes = uiSectors * CDIO_CD_FRAMESIZE_RAW;

    char szWavPath[ROLLER_MAX_PATH];
    SDL_snprintf(szWavPath, ROLLER_MAX_PATH, "%s/track%02u.wav", szAudioDir, (unsigned)t);
    SDL_Log("ExtractAudioTracks: track %u, LSN %d-%d (%u sectors) -> '%s'",
            (unsigned)t, lsnStart, lsnLast, uiSectors, szWavPath);

    FILE *pOut = fopen(szWavPath, "wb");
    if (!pOut) {
      SDL_Log("ExtractAudioTracks: failed to open '%s': %s", szWavPath, strerror(errno));
      continue;
    }

    WriteWAVHeader(pOut, uiDataBytes);

    // Read and write in chunks of 32 sectors for reasonable performance.
    #define AUDIO_CHUNK_SECTORS 32
    uint8 szBuf[CDIO_CD_FRAMESIZE_RAW * AUDIO_CHUNK_SECTORS];
    lsn_t lsn = lsnStart;
    while (lsn <= lsnLast) {
      uint32 uiCount = (uint32)(lsnLast - lsn + 1);
      if (uiCount > AUDIO_CHUNK_SECTORS) uiCount = AUDIO_CHUNK_SECTORS;
      if (cdio_read_audio_sectors(p_cdio, szBuf, lsn, uiCount) != DRIVER_OP_SUCCESS) {
        SDL_Log("ExtractAudioTracks: read error at LSN %d", lsn);
        memset(szBuf, 0, (size_t)uiCount * CDIO_CD_FRAMESIZE_RAW);
      }
      fwrite(szBuf, 1, (size_t)uiCount * CDIO_CD_FRAMESIZE_RAW, pOut);
      lsn += (lsn_t)uiCount;
    }
    #undef AUDIO_CHUNK_SECTORS

    fclose(pOut);
    SDL_Log("ExtractAudioTracks: wrote '%s' (%u bytes)", szWavPath, 44 + uiDataBytes);
  }
}

//-------------------------------------------------------------------------------------------------

static uint64 CueTrackStartByte(const tCueTrackInfo *pTrack)
{
  int iStartSector = pTrack->bHasIndex01 ? pTrack->iIndex01Sector : 0;
  if (iStartSector < 0)
    iStartSector = 0;
  return (uint64)iStartSector * CDIO_CD_FRAMESIZE_RAW;
}

//-------------------------------------------------------------------------------------------------

static uint64 CueTrackEndByte(const tCueInfo *pCueInfo, int iTrackIndex, uint64 uiFileSize)
{
  const tCueTrackInfo *pTrack = &pCueInfo->tracks[iTrackIndex];

  for (int i = iTrackIndex + 1; i < pCueInfo->iNumTracks; i++) {
    const tCueTrackInfo *pNextTrack = &pCueInfo->tracks[i];
    if (!CueStringEqualsIgnoreCase(pTrack->szFile, pNextTrack->szFile))
      continue;

    uint64 uiNextStart = CueTrackStartByte(pNextTrack);
    if (uiNextStart > CueTrackStartByte(pTrack) && uiNextStart <= uiFileSize)
      return uiNextStart;
  }

  return uiFileSize;
}

//-------------------------------------------------------------------------------------------------

static void ExtractAudioTrackFromCue(const tCueInfo *pCueInfo, int iTrackIndex, const char *szAudioDir)
{
  const tCueTrackInfo *pTrack = &pCueInfo->tracks[iTrackIndex];

  if (!pTrack->bBinaryFile) {
    SDL_Log("ExtractAudioTracksFromCue: track %u is not a binary file, skipping", (unsigned)pTrack->uiTrack);
    return;
  }

  FILE *pIn = fopen(pTrack->szFile, "rb");
  if (!pIn) {
    SDL_Log("ExtractAudioTracksFromCue: failed to open '%s': %s", pTrack->szFile, strerror(errno));
    return;
  }

  uint64 uiFileSize;
  if (!GetOpenFileSize(pIn, &uiFileSize)) {
    SDL_Log("ExtractAudioTracksFromCue: failed to determine size of '%s'", pTrack->szFile);
    fclose(pIn);
    return;
  }

  uint64 uiStart = CueTrackStartByte(pTrack);
  uint64 uiEnd = CueTrackEndByte(pCueInfo, iTrackIndex, uiFileSize);
  if (uiStart >= uiFileSize || uiEnd <= uiStart) {
    SDL_Log("ExtractAudioTracksFromCue: track %u has invalid byte range, skipping", (unsigned)pTrack->uiTrack);
    fclose(pIn);
    return;
  }

  uint64 uiDataBytes64 = uiEnd - uiStart;
  if (uiDataBytes64 > 0xffffffffu) {
    SDL_Log("ExtractAudioTracksFromCue: track %u is too large for a WAV file, skipping", (unsigned)pTrack->uiTrack);
    fclose(pIn);
    return;
  }

  if (!SeekOpenFile(pIn, uiStart)) {
    SDL_Log("ExtractAudioTracksFromCue: failed to seek '%s'", pTrack->szFile);
    fclose(pIn);
    return;
  }

  char szWavPath[ROLLER_MAX_PATH];
  SDL_snprintf(szWavPath, ROLLER_MAX_PATH, "%s/track%02u.wav", szAudioDir, (unsigned)pTrack->uiTrack);

  FILE *pOut = fopen(szWavPath, "wb");
  if (!pOut) {
    SDL_Log("ExtractAudioTracksFromCue: failed to open '%s': %s", szWavPath, strerror(errno));
    fclose(pIn);
    return;
  }

  WriteWAVHeader(pOut, (uint32)uiDataBytes64);

  #define AUDIO_FILE_CHUNK_BYTES (CDIO_CD_FRAMESIZE_RAW * 32)
  uint8 szBuf[AUDIO_FILE_CHUNK_BYTES];
  uint64 uiBytesLeft = uiDataBytes64;
  uint64 uiBytesWritten = 0;
  while (uiBytesLeft > 0) {
    size_t nToRead = uiBytesLeft > AUDIO_FILE_CHUNK_BYTES
      ? AUDIO_FILE_CHUNK_BYTES
      : (size_t)uiBytesLeft;
    size_t nRead = fread(szBuf, 1, nToRead, pIn);
    if (nRead == 0)
      break;

    fwrite(szBuf, 1, nRead, pOut);
    uiBytesWritten += nRead;
    uiBytesLeft -= nRead;
  }
  #undef AUDIO_FILE_CHUNK_BYTES

  if (uiBytesWritten != uiDataBytes64) {
    fseek(pOut, 0, SEEK_SET);
    WriteWAVHeader(pOut, (uint32)uiBytesWritten);
  }

  fclose(pOut);
  fclose(pIn);

  SDL_Log("ExtractAudioTracksFromCue: wrote '%s' (%llu bytes)",
          szWavPath, (unsigned long long)(44 + uiBytesWritten));
}

//-------------------------------------------------------------------------------------------------

static void ExtractAudioTracksFromCue(const tCueInfo *pCueInfo, const char *szOutDir)
{
  char szAudioDir[ROLLER_MAX_PATH];
  SDL_snprintf(szAudioDir, ROLLER_MAX_PATH, "%s/audio", szOutDir);
  SDL_CreateDirectory(szAudioDir);

  for (int i = 0; i < pCueInfo->iNumTracks; i++) {
    const tCueTrackInfo *pTrack = &pCueInfo->tracks[i];
    if (pTrack->eKind == CUE_TRACK_AUDIO && pTrack->bHasFile)
      ExtractAudioTrackFromCue(pCueInfo, i, szAudioDir);
  }
}

//-------------------------------------------------------------------------------------------------

void ExtractDir(CdIo_t *p_cdio, const char *szIsoDir, const char *szOutDir)
{
  UpdateSDL();

  SDL_CreateDirectory(szOutDir);

  // iso9660_fs_readdir works via the CdIo driver layer, so Mode 2 XA
  // sectors are handled correctly (unlike iso9660_ifs_readdir on iso9660_t).
  CdioList_t *pList = iso9660_fs_readdir(p_cdio, szIsoDir);
  if (!pList) {
    SDL_Log("ExtractDir: iso9660_fs_readdir failed for '%s'", szIsoDir);
    return;
  }

  SDL_Log("ExtractDir: reading '%s' -> '%s'", szIsoDir, szOutDir);

  CdioListNode_t *pNode;
  _CDIO_LIST_FOREACH(pNode, pList)
  {
    iso9660_stat_t *pStat = (iso9660_stat_t *)_cdio_list_node_data(pNode);

    // strip version number from filename (e.g. "FILE.DAT;1" -> "FILE.DAT")
    char szFilename[ROLLER_MAX_PATH];
    SDL_strlcpy(szFilename, pStat->filename, ROLLER_MAX_PATH);
    char *szSemi = SDL_strchr(szFilename, ';');
    if (szSemi) *szSemi = '\0';

    // skip . and ..
    if (SDL_strcmp(szFilename, ".") == 0 || SDL_strcmp(szFilename, "..") == 0) {
      iso9660_stat_free(pStat);
      continue;
    }

    // skip empty filenames
    if (szFilename[0] == '\0') {
      iso9660_stat_free(pStat);
      continue;
    }

    char szIsoPath[ROLLER_MAX_PATH];
    char szOutPath[ROLLER_MAX_PATH];
    SDL_snprintf(szIsoPath, ROLLER_MAX_PATH, "%s/%s", szIsoDir, szFilename);
    SDL_snprintf(szOutPath, ROLLER_MAX_PATH, "%s/%s", szOutDir, szFilename);

    if (pStat->type == _STAT_DIR) {
      ExtractDir(p_cdio, szIsoPath, szOutPath);
    } else {
      FILE *pOut = fopen(szOutPath, "wb");
      if (pOut) {
        uint32 uiBytesLeft = pStat->size;
        lsn_t lsn = pStat->lsn;
        char szBuf[ISO_BLOCK_SIZE];

        while (uiBytesLeft > 0) {
          memset(szBuf, 0, ISO_BLOCK_SIZE);
          cdio_read_data_sectors(p_cdio, szBuf, lsn, ISO_BLOCK_SIZE, 1);

          uint32 uiToWrite = uiBytesLeft > ISO_BLOCK_SIZE
            ? ISO_BLOCK_SIZE
            : uiBytesLeft;
          fwrite(szBuf, 1, uiToWrite, pOut);

          uiBytesLeft -= uiToWrite;
          lsn++;
        }
        fclose(pOut);
        SDL_Log("  -> wrote '%s' (%u bytes)", szOutPath, pStat->size);
      } else {
        SDL_Log("  -> FAILED to open '%s' for writing: %s", szOutPath, strerror(errno));
      }
    }
    iso9660_stat_free(pStat);
  }
  _cdio_list_free(pList, false, NULL);
}

//-------------------------------------------------------------------------------------------------

char *GetBinPathFromCue(const char *szCuePath, char *szBinPath, int nBufSize)
{
  FILE *pCue = fopen(szCuePath, "r");
  if (!pCue) return NULL;

  char szLine[256];
  while (fgets(szLine, sizeof(szLine), pCue)) {
    char szFile[256];
    if (sscanf(szLine, " FILE \"%255[^\"]\"", szFile) == 1) {
      char szDir[ROLLER_MAX_PATH];
      SDL_strlcpy(szDir, szCuePath, ROLLER_MAX_PATH);
      char *szSlash = SDL_strrchr(szDir, '/');
      if (!szSlash) szSlash = SDL_strrchr(szDir, '\\');
      if (szSlash) {
        *(szSlash + 1) = '\0';
        SDL_snprintf(szBinPath, nBufSize, "%s%s", szDir, szFile);
      } else {
        SDL_strlcpy(szBinPath, szFile, nBufSize);
      }
      fclose(pCue);
      return szBinPath;
    }
  }
  fclose(pCue);
  return NULL;
}

//-------------------------------------------------------------------------------------------------

static bool WriteSingleTrackCue(const char *szSourceCuePath, const tCueTrackInfo *pTrack, char *szCuePath, int nBufSize)
{
  char szTempCueDir[ROLLER_MAX_PATH];
  GetPathDirectory(pTrack->szFile, szTempCueDir, ROLLER_MAX_PATH);
  if (szTempCueDir[0] == '\0')
    GetPathDirectory(szSourceCuePath, szTempCueDir, ROLLER_MAX_PATH);

  if (!BuildTempCuePath(szTempCueDir, szCuePath, nBufSize)) {
    SDL_Log("WriteSingleTrackCue: generated cue path is too long or unavailable");
    return false;
  }

  FILE *pCue = fopen(szCuePath, "w");
  if (!pCue) {
    SDL_Log("WriteSingleTrackCue: failed to open '%s': %s", szCuePath, strerror(errno));
    return false;
  }

  const char *szMode = pTrack->szMode[0] ? pTrack->szMode : "MODE2/2352";
  const char *szBinFilename = GetPathFilename(pTrack->szFile);

  fprintf(pCue, "FILE \"%s\" BINARY\n", szBinFilename);
  fprintf(pCue, "  TRACK 01 %s\n", szMode);
  fprintf(pCue, "    INDEX 01 00:00:00\n");
  fclose(pCue);

  if (pTrack->bHasIndex01 && pTrack->iIndex01Sector != 0) {
    SDL_Log("WriteSingleTrackCue: data track INDEX 01 is not 00:00:00; reading from the file start");
  }

  return true;
}

//-------------------------------------------------------------------------------------------------

static bool ExtractFATDATAFromMultiFileCue(const char *szCuePath, const char *szOutDir, const tCueInfo *pCueInfo)
{
  const tCueTrackInfo *pDataTrack = FindCueDataTrack(pCueInfo);
  if (!pDataTrack) {
    SDL_Log("ExtractFATDATAFromMultiFileCue: no data track found in '%s'", szCuePath);
    return false;
  }

  char szTempCuePath[ROLLER_MAX_PATH];
  if (!WriteSingleTrackCue(szCuePath, pDataTrack, szTempCuePath, ROLLER_MAX_PATH))
    return false;

  char szNormTempCuePath[ROLLER_MAX_PATH];
  NormalizePathForCdio(szTempCuePath, szNormTempCuePath, ROLLER_MAX_PATH);

  SDL_Log("ExtractFATDATAFromMultiFileCue: using track %u '%s' for FATDATA",
          (unsigned)pDataTrack->uiTrack, pDataTrack->szFile);

  CdIo_t *p_cdio = cdio_open_am(szNormTempCuePath, DRIVER_UNKNOWN, NULL);
  if (!p_cdio) {
    SDL_Log("ExtractFATDATAFromMultiFileCue: cdio_open_am failed for '%s'", szNormTempCuePath);
    remove(szTempCuePath);
    return false;
  }

  char szFatdataOut[ROLLER_MAX_PATH];
  SDL_snprintf(szFatdataOut, ROLLER_MAX_PATH, "%s/FATDATA", szOutDir);

  ExtractDir(p_cdio, "/FATDATA", szFatdataOut);
  cdio_destroy(p_cdio);
  remove(szTempCuePath);

  ExtractAudioTracksFromCue(pCueInfo, szOutDir);
  return true;
}

//-------------------------------------------------------------------------------------------------

void ExtractFATDATA(const char *szImagePath, const char *szOutDir)
{
  // Use cdio_open_am with DRIVER_UNKNOWN: auto-detects BIN/CUE, ISO, NRG, etc.
  // Unlike iso9660_open_fuzzy (which reads raw bytes), the CdIo driver layer
  // handles Mode 2 XA sectors (2352-byte, 24-byte header) correctly.
  SDL_Log("ExtractFATDATA: opening '%s', output dir '%s'", szImagePath, szOutDir);

  if (CueStringEndsWithIgnoreCase(szImagePath, ".cue")) {
    tCueInfo cueInfo;
    if (ParseCueFile(szImagePath, &cueInfo) && cueInfo.bMultiFile) {
      SDL_Log("ExtractFATDATA: detected multi-file CUE with %d tracks", cueInfo.iNumTracks);
      ExtractFATDATAFromMultiFileCue(szImagePath, szOutDir, &cueInfo);
      return;
    }
  }

  // libcdio's cdio_dirname (abs_path.c) only recognises '/' as a directory
  // separator, so Windows backslash paths like C:\foo\bar.cue cause it to
  // derive "." as the directory and then fail to find the .BIN file next to
  // the .CUE.  Normalise to forward slashes before handing off to libcdio.
  char szNormPath[ROLLER_MAX_PATH];
  NormalizePathForCdio(szImagePath, szNormPath, ROLLER_MAX_PATH);

  CdIo_t *p_cdio = cdio_open_am(szNormPath, DRIVER_UNKNOWN, NULL);
  if (!p_cdio) {
    SDL_Log("ExtractFATDATA: cdio_open_am failed for '%s'", szNormPath);
    return;
  }
  SDL_Log("ExtractFATDATA: image opened successfully");

  // Write into szOutDir/FATDATA/ so the ROLLERdirexists("./FATDATA") check
  // in InitFATDATA passes after extraction.
  char szFatdataOut[ROLLER_MAX_PATH];
  SDL_snprintf(szFatdataOut, ROLLER_MAX_PATH, "%s/FATDATA", szOutDir);

  ExtractDir(p_cdio, "/FATDATA", szFatdataOut);
  ExtractAudioTracks(p_cdio, szOutDir);
  cdio_destroy(p_cdio);
}

//-------------------------------------------------------------------------------------------------