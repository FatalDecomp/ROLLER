#ifndef _ROLLER_WEB_H
#define _ROLLER_WEB_H

typedef enum {
  ROLLER_WEB_TEXT_DIALOG_NONE = 0,
  ROLLER_WEB_TEXT_DIALOG_NAME,
  ROLLER_WEB_TEXT_DIALOG_REPLAY
} eROLLERWebTextDialog;

int ROLLERWebShowTextDialog(eROLLERWebTextDialog eTarget,
                            const char *szCurrentValue);

#endif
