#ifndef GPU_PARITY_H
#define GPU_PARITY_H

/* Runs the F-S1 windowed/windowless resolved-scene parity matrix.
 * Returns zero only when every fixture satisfies the checked-in metric. */
int ROLLERGpuParityRun(const char *szBackend);

#endif /* GPU_PARITY_H */
