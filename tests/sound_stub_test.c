#include "cdx.h"
#include "roller.h"
#include "rollersound.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>

tColor palette[256];

static int fail(const char *szMessage)
{
  fprintf(stderr, "sound stub test failed: %s\n", szMessage);
  return 1;
}

int main(void)
{
  uint8 abyEncoded[3] = { 0x10u, 0x20u, 0x30u };
  void *pBuffer = (void *)1;
  int16 nSegment = -1;
  void *pDosMemory;

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
    return fail("inert compacted-file query did not fail");

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
