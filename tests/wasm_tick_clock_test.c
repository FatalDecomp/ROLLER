#include "wasm_tick_clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_ten_minute_cadence(void)
{
  const uint64_t ullIntervalNs = 1000000000ULL / 36ULL;
  const uint64_t ullStartNs = 123456789ULL;
  tWasmTickClock clock = { 0 };
  uint64_t ullNowNs = ullStartNs;
  uint64_t ullSteps = 0;

  wasm_tick_clock_reset(&clock, ullNowNs);
  for (int i = 0; i < 36000; ++i) {
    /* Alternate around 60 Hz and include occasional busy frames. The final
     * timestamp, rather than callback count, remains the source of truth. */
    ullNowNs += (i % 2) ? 16666667ULL : 16666666ULL;
    if (i > 0 && i % 997 == 0)
      ullNowNs += 75000000ULL;
    ullSteps += wasm_tick_clock_advance(&clock, ullNowNs, ullIntervalNs, false);
  }

  uint64_t ullElapsedNs = ullNowNs - ullStartNs;
  assert(ullSteps == ullElapsedNs / ullIntervalNs);
  assert(clock.ullRemainderNs == ullElapsedNs % ullIntervalNs);
}

static void test_suspend_discards_elapsed_time(void)
{
  const uint64_t ullIntervalNs = 1000000000ULL / 36ULL;
  tWasmTickClock clock = { 0 };

  wasm_tick_clock_reset(&clock, 1000ULL);
  assert(wasm_tick_clock_advance(&clock, 1000ULL + ullIntervalNs, ullIntervalNs, false) == 1);
  assert(wasm_tick_clock_advance(&clock, 600000000000ULL, ullIntervalNs, true) == 0);
  assert(clock.ullRemainderNs == 0);
  assert(wasm_tick_clock_advance(&clock, 600000000000ULL + ullIntervalNs, ullIntervalNs, false) == 0);
  assert(wasm_tick_clock_advance(&clock, 600000000000ULL + 2 * ullIntervalNs,
      ullIntervalNs, false) == 1);
}

static void test_rate_change_reset(void)
{
  const uint64_t ull50HzNs = 1000000000ULL / 50ULL;
  const uint64_t ull100HzNs = 1000000000ULL / 100ULL;
  tWasmTickClock clock = { 0 };

  wasm_tick_clock_reset(&clock, 500ULL);
  assert(wasm_tick_clock_advance(&clock, 500ULL + ull50HzNs - 1, ull50HzNs, false) == 0);
  wasm_tick_clock_reset(&clock, 500ULL + ull50HzNs - 1);
  assert(wasm_tick_clock_advance(&clock, 500ULL + ull50HzNs - 1 + ull100HzNs,
      ull100HzNs, false) == 1);
}

int main(void)
{
  test_ten_minute_cadence();
  test_suspend_discards_elapsed_time();
  test_rate_change_reset();
  puts("wasm tick clock tests passed");
  return 0;
}
