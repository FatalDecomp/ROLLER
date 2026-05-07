#ifndef _ROLLER_SNAPSHOT_H
#define _ROLLER_SNAPSHOT_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

#define SNAPSHOT_MAX_FRAMES 64

typedef struct
{
  char szReplayName[64];
  char szOutDir[256];
  int iFramesAy[SNAPSHOT_MAX_FRAMES];
  int iNumFrames;
  int iCapturedCount;
  int iMaxFrame;
} tSnapshotConfig;

extern int g_bSnapshotMode;
extern tSnapshotConfig g_SnapshotConfig;

//-------------------------------------------------------------------------------------------------

// Sets the replay file the snapshot driver will load. Truncates if too long.
void SnapshotSetReplay(const char *szReplay);

// Sets the output directory PNGs are written into.
void SnapshotSetOutDir(const char *szOutDir);

// Parses a comma-separated list of non-negative frame indices into the config.
// Returns 0 on success, 1 on malformed input or overflow.
int SnapshotParseFrames(const char *szFramesArg);

// Top-of-tick hook: zeroes scrbuf so unused HUD/border regions can never leak
// from the previous frame. No-op outside snapshot mode.
void SnapshotZeroScreen(void);

// End-of-tick hook called in place of GPU presentation. Writes an indexed PNG
// when currentreplayframe matches a requested frame index. Sets quit_game once
// every requested frame has been captured.
void SnapshotPresent(void);

// Drives the replay one tick per loop iteration in lieu of the SDL tick timer.
// No-op outside snapshot mode.
void SnapshotAdvanceTick(void);

//-------------------------------------------------------------------------------------------------
#endif
