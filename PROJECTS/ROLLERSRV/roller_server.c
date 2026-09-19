#include "net_harness.h"
#include <stdio.h>
int main(int iArgc, char **ppArgv)
{
  int iResult = NetHarnessMain(iArgc, (const char **)ppArgv);
  if (iResult < 0) {
    fputs("E0 server supports --net-harness PORT --track-path FILE --assets-path DIR\n", stderr);
    return 2;
  }
  return iResult;
}
