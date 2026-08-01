#include "editor_api.h"

int main(void)
{
    /* Referencing the public facade pulls its legacy rendering dependency
     * closure from the static archive.  This executable therefore fails to
     * link if roller-core depends on host symbols that it does not provide. */
    return RollerEd_Bootstrap(NULL) == ROLLER_ED_RESULT_INVALID_ARGUMENT
        ? 0 : 1;
}
