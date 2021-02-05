//===-- syscall.cc - Measure empty system call -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains micro-benchmarks to measure the cost of a fast system call.

#include <cstdint>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <sys/syscall.h> /* For SYS_xxx definitions */

#include "stats-timing.h"
#include "target.h"
#include "mlfsr32.h"

using namespace lomp;

static void measureSyscall(statistic * s) {
  int const numRepeats = 250;
  int const innerReps = 5000;

  // Use "system" to ensure that we're not seeing a cached value
  // since some libc implementations do that.
  for (int rep = 0; rep < numRepeats; rep++) {
    TIME_BLOCK(s);
    for (int i = 0; i < innerReps; i++)
      syscall(SYS_getpid);
  }
  s->scaleDown(innerReps);
}

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

int main(int, char **) {
  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

  statistic stat;
  measureSyscall(&stat);

  printf("getpid() time\n"
         "%s\n"
         "# %s"
         "Count,       Min,      Mean,       Max,        SD\n",
         targetName.c_str(), getDateTime().c_str());

  printf("%s\n", stat.format('T').c_str());
  stat.scale(lomp::tsc_tick_count::getTickTime());
  printf("%s\n", stat.format('s').c_str());

  return 0;
}
