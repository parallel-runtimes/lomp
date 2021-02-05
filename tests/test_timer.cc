//===-- test_timer.cc - Test our timer  -------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a unit test for the timer used by the LOMP runtime
/// and its micro-benchmarks.
///
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <chrono>

#include "stats-timing.h"

int main(void) {
  // Very simple test of the timing code.
  // It does show how to print its properties, though.
  printf("Testing %s\n", lomp::tsc_tick_count::timerDescription().c_str());

  // Run it ten times, and allow one failure
  int failures = 0;
  for (int i = 0; i < 10; i++) {
    // Check that it agrees with std::chrono when we measure 1ms.
    auto start = std::chrono::steady_clock::now();
    lomp::tsc_tick_count startTick;
    auto end = start + std::chrono::milliseconds(1);

    while (std::chrono::steady_clock::now() < end) {
    }

    auto elapsed = (lomp::tsc_tick_count::now() - startTick).seconds();
    auto ratio = elapsed / 1.e-3;
    if ((0.995 < ratio) && (ratio < 1.005)) {
      printf("Measured %6.3f ms, %6.4f %% (within 0.5%%) of the std::chrono "
             "time\n",
             elapsed * 1000, ratio * 100.0);
    }
    else {
      printf("Measured %6.3f ms, (outside 0.5%%) of the std::chrono time\n",
             elapsed * 1000);
      failures++;
    }
  }
  printf("%d failures; we allow one...\n", failures);
  printf("***%s***\n", failures > 1 ? "FAILED" : "PASSED");
  return !failures ? EXIT_SUCCESS : EXIT_FAILURE;
}
