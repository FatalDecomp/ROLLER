#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include "crashdump.h"
#include "types.h"

#if defined(__has_include)
#if __has_include("build_info.h")
#include "build_info.h"
#else
#include "build_info_default.h"
#endif
#else
#include "build_info_default.h"
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#else
#include <dirent.h>
#if !defined(IS_ANDROID)
#include <execinfo.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#ifdef IS_MACOS
#include <mach-o/dyld.h>
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

//-------------------------------------------------------------------------------------------------

#define CRASHDUMP_KEEP_COUNT 10
#define CRASHDUMP_MAX_ROTATE_FILES 128
#define CRASHDUMP_MAX_PATH 1024
#define CRASHDUMP_MAX_NAME 256
#define CRASHDUMP_REPORT_HEADER_SIZE 2048
#define CRASHDUMP_DYLD_INFO_SIZE 32768
#define CRASHDUMP_ALT_STACK_SIZE (64 * 1024)

//-------------------------------------------------------------------------------------------------

typedef struct
{
  char szName[CRASHDUMP_MAX_NAME];
  char szPath[CRASHDUMP_MAX_PATH];
} tCrashFileEntry;

//-------------------------------------------------------------------------------------------------

static char s_szCrashDir[CRASHDUMP_MAX_PATH];
static char s_szFallbackCrashDir[CRASHDUMP_MAX_PATH];
static char s_szCrashPath[CRASHDUMP_MAX_PATH];
static char s_szFallbackCrashPath[CRASHDUMP_MAX_PATH];
static char s_szCrashBaseName[CRASHDUMP_MAX_NAME];
static char s_szBuildInfo[CRASHDUMP_REPORT_HEADER_SIZE];
static char s_szReportHeader[CRASHDUMP_REPORT_HEADER_SIZE];

#ifdef IS_WINDOWS
static volatile LONG s_lHandlingCrash = 0;
#else
static volatile sig_atomic_t s_iHandlingCrash = 0;
static uint8 s_abyAltStack[CRASHDUMP_ALT_STACK_SIZE];
#ifdef IS_MACOS
static char s_szDyldInfo[CRASHDUMP_DYLD_INFO_SIZE];
static int s_iDyldInfoLen = 0;
#endif
#endif

//-------------------------------------------------------------------------------------------------

static int IsEnvEnabled(const char *szName)
{
  const char *pszValue = getenv(szName);
  if (!pszValue || !pszValue[0])
    return 0;
  if (strcmp(pszValue, "0") == 0)
    return 0;
  if (strcmp(pszValue, "false") == 0 || strcmp(pszValue, "FALSE") == 0)
    return 0;
  if (strcmp(pszValue, "no") == 0 || strcmp(pszValue, "NO") == 0)
    return 0;
  return 1;
}

//-------------------------------------------------------------------------------------------------

static int ShouldEnableCrashHandler(void)
{
  const char *pszForce = getenv("ROLLER_CRASH_HANDLER");

  if (IsEnvEnabled("ROLLER_NO_CRASH_HANDLER"))
    return 0;
  if (pszForce)
    return IsEnvEnabled("ROLLER_CRASH_HANDLER");
  if (strcmp(BUILD_VERSION, "local") == 0)
    return 1;
  if (strstr(BUILD_VERSION, "-dev") != NULL)
    return 1;

  return 0;
}

//-------------------------------------------------------------------------------------------------

static char GetPathSeparator(void)
{
#ifdef IS_WINDOWS
  return '\\';
#else
  return '/';
#endif
}

//-------------------------------------------------------------------------------------------------

static void StripFilename(char *szPath)
{
  char *pszSlash = strrchr(szPath, '/');
  char *pszBackslash = strrchr(szPath, '\\');
  char *pszSep = pszSlash;

  if (pszBackslash && (!pszSep || pszBackslash > pszSep))
    pszSep = pszBackslash;
  if (pszSep)
    *pszSep = '\0';
}

//-------------------------------------------------------------------------------------------------

static void BuildPath(char *szOut, int iOutSize, const char *szDir, const char *szName)
{
  char cSep = GetPathSeparator();
  int iLen = (int)strlen(szDir);

  if (!szDir[0]) {
    snprintf(szOut, iOutSize, "%s", szName);
    return;
  }

  if (szDir[iLen - 1] == '/' || szDir[iLen - 1] == '\\')
    snprintf(szOut, iOutSize, "%s%s", szDir, szName);
  else
    snprintf(szOut, iOutSize, "%s%c%s", szDir, cSep, szName);
}

//-------------------------------------------------------------------------------------------------

static int IsCrashFileName(const char *szName)
{
  return strncmp(szName, "roller-crash-", 13) == 0;
}

//-------------------------------------------------------------------------------------------------

static void AddCrashFileEntry(tCrashFileEntry *pEntries, int *piCount, const char *szDir, const char *szName)
{
  int iCount = *piCount;
  int iInsert;

  if (iCount >= CRASHDUMP_MAX_ROTATE_FILES)
    return;

  for (iInsert = iCount; iInsert > 0; --iInsert) {
    if (strcmp(pEntries[iInsert - 1].szName, szName) <= 0)
      break;
    pEntries[iInsert] = pEntries[iInsert - 1];
  }

  snprintf(pEntries[iInsert].szName, sizeof(pEntries[iInsert].szName), "%s", szName);
  BuildPath(pEntries[iInsert].szPath, sizeof(pEntries[iInsert].szPath), szDir, szName);
  *piCount = iCount + 1;
}

//-------------------------------------------------------------------------------------------------

#ifdef IS_WINDOWS
static void CreateDirIfNeeded(const char *szDir)
{
  if (szDir[0])
    CreateDirectoryA(szDir, NULL);
}

//-------------------------------------------------------------------------------------------------

static void RotateCrashFiles(const char *szDir)
{
  tCrashFileEntry crashFileEntryAy[CRASHDUMP_MAX_ROTATE_FILES];
  WIN32_FIND_DATAA stFindData;
  HANDLE hFind;
  char szSearchPath[CRASHDUMP_MAX_PATH];
  int iCount = 0;
  int iDeleteCount;

  if (!szDir[0])
    return;

  BuildPath(szSearchPath, sizeof(szSearchPath), szDir, "roller-crash-*");
  hFind = FindFirstFileA(szSearchPath, &stFindData);
  if (hFind == INVALID_HANDLE_VALUE)
    return;

  do {
    if ((stFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
        IsCrashFileName(stFindData.cFileName)) {
      AddCrashFileEntry(crashFileEntryAy, &iCount, szDir, stFindData.cFileName);
    }
  } while (FindNextFileA(hFind, &stFindData));

  FindClose(hFind);

  iDeleteCount = iCount - CRASHDUMP_KEEP_COUNT;
  for (int i = 0; i < iDeleteCount; ++i)
    DeleteFileA(crashFileEntryAy[i].szPath);
}

//-------------------------------------------------------------------------------------------------

static void ResolveExecutableDir(char *szOut, int iOutSize, const char *szDataRoot)
{
  (void)szDataRoot;
  DWORD dwLen = GetModuleFileNameA(NULL, szOut, (DWORD)iOutSize);
  if (dwLen == 0 || dwLen >= (DWORD)iOutSize) {
    snprintf(szOut, iOutSize, ".");
    return;
  }
  StripFilename(szOut);
}

//-------------------------------------------------------------------------------------------------

static void ResolveFallbackCrashDir(char *szOut, int iOutSize, const char *szDataRoot)
{
  const char *pszLocalAppData = getenv("LOCALAPPDATA");
  char szRollerDir[CRASHDUMP_MAX_PATH];

  (void)szDataRoot;
  szOut[0] = '\0';
  if (!pszLocalAppData || !pszLocalAppData[0])
    return;

  BuildPath(szRollerDir, sizeof(szRollerDir), pszLocalAppData, "ROLLER");
  CreateDirIfNeeded(szRollerDir);
  BuildPath(szOut, iOutSize, szRollerDir, "crashes");
  CreateDirIfNeeded(szOut);
}

//-------------------------------------------------------------------------------------------------

static int WriteMiniDump(const char *szPath, EXCEPTION_POINTERS *pException)
{
  HANDLE hFile;
  MINIDUMP_EXCEPTION_INFORMATION stExceptionInfo;
  MINIDUMP_USER_STREAM stUserStream;
  MINIDUMP_USER_STREAM_INFORMATION stUserStreamInfo;
  MINIDUMP_TYPE eDumpType;
  BOOL bOk;

  hFile = CreateFileA(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return 0;

  stExceptionInfo.ThreadId = GetCurrentThreadId();
  stExceptionInfo.ExceptionPointers = pException;
  stExceptionInfo.ClientPointers = FALSE;

  stUserStream.Type = CommentStreamA;
  stUserStream.BufferSize = (ULONG)(strlen(s_szBuildInfo) + 1);
  stUserStream.Buffer = s_szBuildInfo;

  stUserStreamInfo.UserStreamCount = 1;
  stUserStreamInfo.UserStreamArray = &stUserStream;

  eDumpType = (MINIDUMP_TYPE)(MiniDumpWithDataSegs |
                              MiniDumpWithIndirectlyReferencedMemory |
                              MiniDumpWithThreadInfo);

  bOk = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, eDumpType,
                          &stExceptionInfo, &stUserStreamInfo, NULL);
  CloseHandle(hFile);

  return bOk != FALSE;
}

//-------------------------------------------------------------------------------------------------

static LONG WINAPI CrashUnhandledExceptionFilter(EXCEPTION_POINTERS *pException)
{
  const char *pszWrittenPath = NULL;

  if (InterlockedExchange(&s_lHandlingCrash, 1) != 0)
    return EXCEPTION_EXECUTE_HANDLER;

  if (WriteMiniDump(s_szCrashPath, pException)) {
    pszWrittenPath = s_szCrashPath;
  } else if (s_szFallbackCrashPath[0] && WriteMiniDump(s_szFallbackCrashPath, pException)) {
    pszWrittenPath = s_szFallbackCrashPath;
  }

  if (pszWrittenPath) {
    char szMessage[CRASHDUMP_MAX_PATH + 256];
    snprintf(szMessage, sizeof(szMessage),
             "ROLLER has crashed.\n\nA crash dump was saved to:\n%s\n\n"
             "Please attach this file to a GitHub crash report.",
             pszWrittenPath);
    MessageBoxA(NULL, szMessage, "ROLLER crash", MB_OK | MB_ICONERROR | MB_TASKMODAL);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

#else

static void CreateDirIfNeeded(const char *szDir)
{
  if (szDir[0])
    mkdir(szDir, 0755);
}

//-------------------------------------------------------------------------------------------------

static void RotateCrashFiles(const char *szDir)
{
  tCrashFileEntry crashFileEntryAy[CRASHDUMP_MAX_ROTATE_FILES];
  DIR *pDir;
  struct dirent *pEntry;
  int iCount = 0;
  int iDeleteCount;

  if (!szDir[0])
    return;

  pDir = opendir(szDir);
  if (!pDir)
    return;

  while ((pEntry = readdir(pDir)) != NULL) {
    if (IsCrashFileName(pEntry->d_name))
      AddCrashFileEntry(crashFileEntryAy, &iCount, szDir, pEntry->d_name);
  }

  closedir(pDir);

  iDeleteCount = iCount - CRASHDUMP_KEEP_COUNT;
  for (int i = 0; i < iDeleteCount; ++i)
    unlink(crashFileEntryAy[i].szPath);
}

//-------------------------------------------------------------------------------------------------

static void ResolveExecutableDir(char *szOut, int iOutSize, const char *szDataRoot)
{
#if defined(IS_ANDROID)
  if (szDataRoot && szDataRoot[0]) {
    BuildPath(szOut, iOutSize, szDataRoot, "crashes");
    CreateDirIfNeeded(szOut);
    return;
  }
#elif defined(IS_LINUX)
  ssize_t llLen = readlink("/proc/self/exe", szOut, (size_t)iOutSize - 1);
  if (llLen > 0 && llLen < iOutSize) {
    szOut[llLen] = '\0';
    StripFilename(szOut);
    return;
  }
#elif defined(IS_MACOS)
  uint32 uiSize = (uint32)iOutSize;
  if (_NSGetExecutablePath(szOut, &uiSize) == 0) {
    StripFilename(szOut);
    return;
  }
#endif

  if (!getcwd(szOut, (size_t)iOutSize))
    snprintf(szOut, iOutSize, ".");
}

//-------------------------------------------------------------------------------------------------

static void ResolveFallbackCrashDir(char *szOut, int iOutSize, const char *szDataRoot)
{
  const char *pszHome;

  szOut[0] = '\0';

#if defined(IS_ANDROID)
  if (szDataRoot && szDataRoot[0]) {
    BuildPath(szOut, iOutSize, szDataRoot, "crashes");
    CreateDirIfNeeded(szOut);
  }
#elif defined(IS_MACOS)
  pszHome = getenv("HOME");
  if (pszHome && pszHome[0]) {
    char szLogsDir[CRASHDUMP_MAX_PATH];
    BuildPath(szLogsDir, sizeof(szLogsDir), pszHome, "Library/Logs");
    BuildPath(szOut, iOutSize, szLogsDir, "ROLLER");
    CreateDirIfNeeded(szOut);
  }
#else
  const char *pszStateHome = getenv("XDG_STATE_HOME");
  char szStateDir[CRASHDUMP_MAX_PATH];

  if (pszStateHome && pszStateHome[0]) {
    snprintf(szStateDir, sizeof(szStateDir), "%s", pszStateHome);
  } else {
    pszHome = getenv("HOME");
    if (!pszHome || !pszHome[0])
      return;
    BuildPath(szStateDir, sizeof(szStateDir), pszHome, ".local/state");
    CreateDirIfNeeded(szStateDir);
  }

  {
    char szRollerDir[CRASHDUMP_MAX_PATH];
    BuildPath(szRollerDir, sizeof(szRollerDir), szStateDir, "ROLLER");
    CreateDirIfNeeded(szRollerDir);
    BuildPath(szOut, iOutSize, szRollerDir, "crashes");
    CreateDirIfNeeded(szOut);
  }
#endif
}

//-------------------------------------------------------------------------------------------------

static void WriteBuffer(int iFd, const char *pBuf, size_t uiLen)
{
  while (uiLen > 0) {
    ssize_t llWritten = write(iFd, pBuf, uiLen);
    if (llWritten < 0) {
      if (errno == EINTR)
        continue;
      return;
    }
    if (llWritten == 0)
      return;
    pBuf += llWritten;
    uiLen -= (size_t)llWritten;
  }
}

//-------------------------------------------------------------------------------------------------

static void WriteCString(int iFd, const char *szText)
{
  const char *pEnd = szText;
  while (*pEnd)
    ++pEnd;
  WriteBuffer(iFd, szText, (size_t)(pEnd - szText));
}

//-------------------------------------------------------------------------------------------------

#define WriteLiteral(iFd, szText) WriteBuffer((iFd), (szText), sizeof(szText) - 1)

//-------------------------------------------------------------------------------------------------

static void WriteUIntDec(int iFd, uint64 ullVal)
{
  char szBuf[32];
  int iPos = (int)sizeof(szBuf);

  szBuf[--iPos] = '\0';
  do {
    szBuf[--iPos] = (char)('0' + (ullVal % 10));
    ullVal /= 10;
  } while (ullVal > 0 && iPos > 0);

  WriteCString(iFd, &szBuf[iPos]);
}

//-------------------------------------------------------------------------------------------------

static void WriteIntDec(int iFd, int iVal)
{
  if (iVal < 0) {
    WriteLiteral(iFd, "-");
    WriteUIntDec(iFd, (uint64)(-(int64)iVal));
  } else {
    WriteUIntDec(iFd, (uint64)iVal);
  }
}

//-------------------------------------------------------------------------------------------------

static void WriteHex64(int iFd, uint64 ullVal)
{
  static const char s_szHex[] = "0123456789abcdef";
  char szBuf[18];

  szBuf[0] = '0';
  szBuf[1] = 'x';
  for (int i = 0; i < 16; ++i) {
    int iShift = 60 - i * 4;
    szBuf[2 + i] = s_szHex[(ullVal >> iShift) & 0xF];
  }

  WriteBuffer(iFd, szBuf, sizeof(szBuf));
}

//-------------------------------------------------------------------------------------------------

static void GetSignalContextRegisters(void *pContext, uint64 *pullPc, uint64 *pullSp)
{
  *pullPc = 0;
  *pullSp = 0;

#if (defined(IS_LINUX) || defined(IS_ANDROID)) && defined(__x86_64__)
  {
    ucontext_t *pUContext = (ucontext_t *)pContext;
    *pullPc = (uint64)pUContext->uc_mcontext.gregs[REG_RIP];
    *pullSp = (uint64)pUContext->uc_mcontext.gregs[REG_RSP];
  }
#elif (defined(IS_LINUX) || defined(IS_ANDROID)) && defined(__aarch64__)
  {
    ucontext_t *pUContext = (ucontext_t *)pContext;
    *pullPc = (uint64)pUContext->uc_mcontext.pc;
    *pullSp = (uint64)pUContext->uc_mcontext.sp;
  }
#elif defined(IS_MACOS) && defined(__x86_64__)
  {
    ucontext_t *pUContext = (ucontext_t *)pContext;
    *pullPc = (uint64)pUContext->uc_mcontext->__ss.__rip;
    *pullSp = (uint64)pUContext->uc_mcontext->__ss.__rsp;
  }
#elif defined(IS_MACOS) && (defined(__aarch64__) || defined(__arm64__))
  {
    ucontext_t *pUContext = (ucontext_t *)pContext;
    *pullPc = (uint64)pUContext->uc_mcontext->__ss.__pc;
    *pullSp = (uint64)pUContext->uc_mcontext->__ss.__sp;
  }
#else
  (void)pContext;
#endif
}

//-------------------------------------------------------------------------------------------------

static void WriteDescriptorContents(int iOutFd, int iInFd)
{
  char szBuf[2048];

  for (;;) {
    ssize_t llRead = read(iInFd, szBuf, sizeof(szBuf));
    if (llRead < 0) {
      if (errno == EINTR)
        continue;
      return;
    }
    if (llRead == 0)
      return;
    WriteBuffer(iOutFd, szBuf, (size_t)llRead);
  }
}

//-------------------------------------------------------------------------------------------------

static void WriteMappings(int iFd)
{
  WriteLiteral(iFd, "\nMappings:\n");

#if defined(IS_LINUX) || defined(IS_ANDROID)
  {
    int iMapsFd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (iMapsFd >= 0) {
      WriteDescriptorContents(iFd, iMapsFd);
      close(iMapsFd);
    } else {
      WriteLiteral(iFd, "Could not open /proc/self/maps\n");
    }
  }
#elif defined(IS_MACOS)
  if (s_iDyldInfoLen > 0)
    WriteBuffer(iFd, s_szDyldInfo, (size_t)s_iDyldInfoLen);
  else
    WriteLiteral(iFd, "No dyld image list captured\n");
#else
  WriteLiteral(iFd, "Module mapping not available on this platform\n");
#endif
}

//-------------------------------------------------------------------------------------------------

static void WriteBacktrace(int iFd)
{
#if defined(IS_ANDROID)
  WriteLiteral(iFd, "\nBacktrace:\n");
  WriteLiteral(iFd, "Backtrace symbols not available on Android\n");
#else
  void *pFrames[64];
  int iFrameCount;

  WriteLiteral(iFd, "\nBacktrace:\n");
  iFrameCount = backtrace(pFrames, (int)(sizeof(pFrames) / sizeof(pFrames[0])));
  if (iFrameCount > 0)
    backtrace_symbols_fd(pFrames, iFrameCount, iFd);
  else
    WriteLiteral(iFd, "No frames captured\n");
#endif
}

//-------------------------------------------------------------------------------------------------

static void RestoreDefaultAndReraise(int iSig)
{
  struct sigaction stAction;
  sigset_t stSigSet;

  stAction.sa_handler = SIG_DFL;
  stAction.sa_flags = 0;
  sigemptyset(&stAction.sa_mask);
  sigaction(iSig, &stAction, NULL);

  sigemptyset(&stSigSet);
  sigaddset(&stSigSet, iSig);
  sigprocmask(SIG_UNBLOCK, &stSigSet, NULL);

  kill(getpid(), iSig);
  _exit(128 + iSig);
}

//-------------------------------------------------------------------------------------------------

static void CrashSignalHandler(int iSig, siginfo_t *pSigInfo, void *pContext)
{
  int iFd;
  uint64 ullFaultAddr = 0;
  uint64 ullPc = 0;
  uint64 ullSp = 0;

  if (s_iHandlingCrash)
    _exit(128 + iSig);
  s_iHandlingCrash = 1;

  iFd = open(s_szCrashPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (iFd < 0 && s_szFallbackCrashPath[0])
    iFd = open(s_szFallbackCrashPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

  if (iFd >= 0) {
    if (pSigInfo)
      ullFaultAddr = (uint64)(size_t)pSigInfo->si_addr;
    GetSignalContextRegisters(pContext, &ullPc, &ullSp);

    WriteCString(iFd, s_szReportHeader);
    WriteLiteral(iFd, "Signal: ");
    WriteIntDec(iFd, iSig);
    WriteLiteral(iFd, "\nsi_code: ");
    WriteIntDec(iFd, pSigInfo ? pSigInfo->si_code : 0);
    WriteLiteral(iFd, "\nFault address: ");
    WriteHex64(iFd, ullFaultAddr);
    WriteLiteral(iFd, "\nProgram counter: ");
    WriteHex64(iFd, ullPc);
    WriteLiteral(iFd, "\nStack pointer: ");
    WriteHex64(iFd, ullSp);
    WriteLiteral(iFd, "\n");

    WriteBacktrace(iFd);
    WriteMappings(iFd);
    close(iFd);
  }

  RestoreDefaultAndReraise(iSig);
}

//-------------------------------------------------------------------------------------------------

static void WarmUpBacktrace(void)
{
#if defined(IS_ANDROID)
  return;
#else
  int iFd = open("/dev/null", O_WRONLY | O_CLOEXEC);
  void *pFrames[1];
  int iFrameCount = backtrace(pFrames, 1);

  if (iFd >= 0) {
    if (iFrameCount > 0)
      backtrace_symbols_fd(pFrames, iFrameCount, iFd);
    close(iFd);
  }
#endif
}

//-------------------------------------------------------------------------------------------------

#ifdef IS_MACOS
static void CaptureDyldInfo(void)
{
  uint32 uiCount = _dyld_image_count();
  int iOffset = 0;

  iOffset += snprintf(s_szDyldInfo + iOffset, sizeof(s_szDyldInfo) - (size_t)iOffset,
                      "Dyld images:\n");

  for (uint32 i = 0; i < uiCount && iOffset < (int)sizeof(s_szDyldInfo) - 1; ++i) {
    const struct mach_header *pMachHeader = _dyld_get_image_header(i);
    const char *szName = _dyld_get_image_name(i);
    long long llSlide = (long long)_dyld_get_image_vmaddr_slide(i);
    iOffset += snprintf(s_szDyldInfo + iOffset, sizeof(s_szDyldInfo) - (size_t)iOffset,
                        "%u header=0x%llx slide=%lld %s\n",
                        i,
                        (unsigned long long)(size_t)pMachHeader,
                        llSlide,
                        szName ? szName : "");
  }

  if (iOffset >= (int)sizeof(s_szDyldInfo))
    iOffset = (int)sizeof(s_szDyldInfo) - 1;
  s_szDyldInfo[iOffset] = '\0';
  s_iDyldInfoLen = iOffset;
}
#endif

//-------------------------------------------------------------------------------------------------

static void InstallPosixCrashHandlers(void)
{
  const int iSignalAy[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
  stack_t stAltStack;
  struct sigaction stAction;

  stAltStack.ss_sp = s_abyAltStack;
  stAltStack.ss_size = sizeof(s_abyAltStack);
  stAltStack.ss_flags = 0;
  sigaltstack(&stAltStack, NULL);

  memset(&stAction, 0, sizeof(stAction));
  stAction.sa_sigaction = CrashSignalHandler;
  stAction.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&stAction.sa_mask);

  for (int i = 0; i < (int)(sizeof(iSignalAy) / sizeof(iSignalAy[0])); ++i)
    sigaction(iSignalAy[i], &stAction, NULL);
}
#endif

//-------------------------------------------------------------------------------------------------

static void BuildCrashBaseName(const char *szExt)
{
#ifdef IS_WINDOWS
  SYSTEMTIME stSystemTime;
  GetLocalTime(&stSystemTime);
  snprintf(s_szCrashBaseName, sizeof(s_szCrashBaseName),
           "roller-crash-%04u%02u%02u-%02u%02u%02u%s",
           (unsigned)stSystemTime.wYear,
           (unsigned)stSystemTime.wMonth,
           (unsigned)stSystemTime.wDay,
           (unsigned)stSystemTime.wHour,
           (unsigned)stSystemTime.wMinute,
           (unsigned)stSystemTime.wSecond,
           szExt);
#else
  time_t tNow = time(NULL);
  struct tm stTmNow;
  struct tm *pTm = localtime_r(&tNow, &stTmNow);
  if (!pTm) {
    memset(&stTmNow, 0, sizeof(stTmNow));
    stTmNow.tm_year = 70;
    stTmNow.tm_mday = 1;
  }

  snprintf(s_szCrashBaseName, sizeof(s_szCrashBaseName),
           "roller-crash-%04d%02d%02d-%02d%02d%02d%s",
           stTmNow.tm_year + 1900,
           stTmNow.tm_mon + 1,
           stTmNow.tm_mday,
           stTmNow.tm_hour,
           stTmNow.tm_min,
           stTmNow.tm_sec,
           szExt);
#endif
}

//-------------------------------------------------------------------------------------------------

static void BuildStaticCrashInfo(void)
{
  snprintf(s_szBuildInfo, sizeof(s_szBuildInfo),
           "ROLLER crash build information\n"
           "Version: %s\n"
           "Git hash: %s\n"
           "Build date: %s\n"
           "Target: %s\n",
           BUILD_VERSION,
           BUILD_GIT_HASH,
           BUILD_DATE,
           BUILD_TARGET);

  snprintf(s_szReportHeader, sizeof(s_szReportHeader),
           "ROLLER crash report\n"
           "Version: %s\n"
           "Git hash: %s\n"
           "Build date: %s\n"
           "Target: %s\n"
           "Report path: %s\n\n",
           BUILD_VERSION,
           BUILD_GIT_HASH,
           BUILD_DATE,
           BUILD_TARGET,
           s_szCrashPath);
}

//-------------------------------------------------------------------------------------------------

void InitCrashHandler(const char *szDataRoot)
{
  if (!ShouldEnableCrashHandler())
    return;

  ResolveExecutableDir(s_szCrashDir, sizeof(s_szCrashDir), szDataRoot);
  ResolveFallbackCrashDir(s_szFallbackCrashDir, sizeof(s_szFallbackCrashDir), szDataRoot);

#ifdef IS_WINDOWS
  BuildCrashBaseName(".dmp");
#else
  BuildCrashBaseName(".txt");
#endif

  BuildPath(s_szCrashPath, sizeof(s_szCrashPath), s_szCrashDir, s_szCrashBaseName);
  if (s_szFallbackCrashDir[0])
    BuildPath(s_szFallbackCrashPath, sizeof(s_szFallbackCrashPath), s_szFallbackCrashDir, s_szCrashBaseName);

  BuildStaticCrashInfo();
  RotateCrashFiles(s_szCrashDir);
  RotateCrashFiles(s_szFallbackCrashDir);

#ifdef IS_WINDOWS
  SetUnhandledExceptionFilter(CrashUnhandledExceptionFilter);
#else
  WarmUpBacktrace();
#ifdef IS_MACOS
  CaptureDyldInfo();
#endif
  InstallPosixCrashHandlers();
#endif
}
