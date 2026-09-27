#ifndef ROLLER_NET_LEGACY_H
#define ROLLER_NET_LEGACY_H

/* Debug guard for the retired lockstep networking surface.  Every exported
   network.c and rollercomms.c entry point passes through this guard. */
void NetLegacyTrapReset(void);
int NetLegacyTrapEntryCount(void);
int NetLegacyTrapViolationCount(void);

#endif
