//===-- yield.cc - Measure yield/pause -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains micro-benchmarks to measure the time a thread is actually
/// delayed for when asking for a nanosleep with different delay values.

#include <cstdint>
#include <ctime>
#include <cstring>
#include <unistd.h>

#include "stats-timing.h"
#include "target.h"
#include <time.h>

using namespace lomp;

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

enum { NUM_REPEATS = 1000 };

int main(int, char **) {
  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

  printf("nanosleep time\n"
         "%s\n"
         "# %s"
         "Requested,  Count,       Min,      Mean,       Max,        SD\n",
         targetName.c_str(), getDateTime().c_str());

  int elapsed[] = {0, 1, 2, 5, 10};
  int base = 0;
  for (int scale = 100; scale < 1000000; scale *= 10) {
    for (int i = base; i < 5; i++) {
      int delay = scale * elapsed[i];
      statistic stat;

      for (int j = 0; j < NUM_REPEATS; j++) {
        timespec dt = {0, delay};
        timespec left;
        TIME_BLOCK(&stat);
        while (nanosleep(&dt, &left) != 0) {
          dt = left;
        }
      }
      stat.scale(lomp::tsc_tick_count::getTickTime());
      printf("%s, %s\n", formatSI(delay * 1.e-9, 8, 's').c_str(),
             stat.format('s').c_str());
      base = 2;
    }
  }

  return 0;
}
