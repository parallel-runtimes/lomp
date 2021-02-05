//===----------------------------------------------------------------------===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/** @file trivial.cc
 * A very simple test code to check that the statistics and timing interfaces
 * work.
 */

#include "cstdio"
#include "stats-timing.h"

int main(int, char **) {
  lomp::statistic stat;
  for (int i = 0; i < 20; i++) {
    TIME_BLOCK(&stat);
    fprintf(stderr, "Hello World\n");
  }
  printf("%s\n", stat.format('T').c_str());
  stat.scale(lomp::tsc_tick_count::getTickTime());
  printf("%s\n", stat.format('s').c_str());
}
