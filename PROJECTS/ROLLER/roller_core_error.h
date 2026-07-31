#ifndef ROLLER_CORE_ERROR_H
#define ROLLER_CORE_ERROR_H

/* Internal roller-core replacement for the game's fatal dialog boundary. */
void ErrorBoxExit(const char *szErrorMsgFormat, ...);

void roller_core_error_clear(void);
int roller_core_error_pending(void);
const char *roller_core_error_message(void);

#endif
