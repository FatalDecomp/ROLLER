#ifndef GPU_PARITY_H
#define GPU_PARITY_H

/* Runs the F-S1 windowed/windowless resolved-scene parity matrix plus the
 * chained F-S4a depth, F-S4b selection, and F-S5 canonical-emission checks.
 * Returns zero only when every check passes. */
int ROLLERGpuParityRun(const char *szBackend);

#endif /* GPU_PARITY_H */
