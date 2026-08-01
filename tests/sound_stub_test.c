#include "cdx.h"
#include "roller.h"
#include "rollersound.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

tColor palette[256];

static int fail(const char *szMessage)
{
  fprintf(stderr, "sound stub test failed: %s\n", szMessage);
  return 1;
}

static int write_compacted_fixture(const char *szPath)
{
  static const uint8 abyFixture[] = {
    6u, 0u, 0u, 0u,
    3u, 0x10u, 0x20u, 0x30u,
    0x80u,
    0u
  };
  FILE *pFile = fopen(szPath, "wb");

  if (!pFile)
    return 0;
  if (fwrite(abyFixture, 1u, sizeof(abyFixture), pFile)
      != sizeof(abyFixture)) {
    fclose(pFile);
    return 0;
  }
  return fclose(pFile) == 0;
}

int main(int argc, char **argv)
{
  uint8 abyEncoded[3] = { 0x10u, 0x20u, 0x30u };
  uint8 abyCompacted[6] = { 0u };
  uint8 abyStreamed[40006] = { 0u };
  char szFixturePath[512];
  void *pBuffer = (void *)1;
  int16 nSegment = -1;
  void *pDosMemory;

  if (argc != 2)
    return fail("expected temporary output directory argument");
  if (snprintf(szFixturePath, sizeof(szFixturePath),
               "%s/sound_stub_compacted_fixture.tmp", argv[1])
      < 0)
    return fail("could not form compacted fixture path");
  if (!write_compacted_fixture(szFixturePath))
    return fail("could not write compacted fixture");

  (void)&iTicksPending;

  Initialise_SOS();
  if (soundon || musicon || MusicBackendAvailable())
    return fail("audio must remain disabled");
  if (MIDI_Init("not-used") || MIDI_OS_Init() || MIDI_OPL_Init())
    return fail("MIDI backends must remain unavailable");
  if (DIGISampleStart(NULL) != -1 || !DIGISampleDone(-1))
    return fail("digital sample backend must remain unavailable");

  claim_ticktimer(50u);
  if (ullTickIntervalNs != HZ_TO_NS(50u))
    return fail("tick interval was not retained");

  fade_palette_begin(17);
  if (palette_brightness != 17 || fade_palette_active() != 0 ||
      fade_palette_update() != -1)
    return fail("palette fade did not complete immediately");

  if (loadDOS("not-used", &pBuffer) || pBuffer != NULL)
    return fail("inert file load returned caller-owned data");
  if (getcompactedfilelength("not-used") != -1)
    return fail("missing compacted-file query did not fail");
  if (getcompactedfilelength(szFixturePath) != 6)
    return fail("compacted-file length was not decoded");
  if (loadcompactedfile(szFixturePath, abyCompacted) != 0
      || memcmp(abyCompacted, "\x10\x20\x30\x10\x20\x30", 6u) != 0)
    return fail("compacted asset did not decode");
  if (initmangle(szFixturePath) != 0)
    return fail("compacted streaming initialization failed");
  readmangled(abyStreamed, 6);
  if (memcmp(abyStreamed + 40000,
             "\x10\x20\x30\x10\x20\x30", 6u) != 0)
    return fail("compacted streaming output changed");
  if (uninitmangle() != 0)
    return fail("compacted streaming cleanup failed");
  if (remove(szFixturePath) != 0)
    return fail("could not remove compacted fixture");

  decode(abyEncoded, 3, 1u, 2u);
  if (abyEncoded[0] != 0x13u || abyEncoded[1] != 0x25u ||
      abyEncoded[2] != 0x38u)
    return fail("shared XOR decoder behavior changed");

  cdxinit();
  if (cdpresent() != 0 || track_playing != 0 || numCDdrives != 0)
    return fail("CD audio must remain unavailable");

  pDosMemory = AllocDOSMemory(16, &nSegment);
  if (!pDosMemory || nSegment != 0)
    return fail("compatibility allocation failed");
  free(pDosMemory);

  printf("roller-core sound stubs passed\n");
  return 0;
}
