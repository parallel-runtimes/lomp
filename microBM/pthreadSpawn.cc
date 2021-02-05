//===-- threadSpawn.cc - Measure time to create a thread -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a micro-benchmark to measure the time to create thread.
///
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include "stats-timing.h"

void * doNothing(void *) {
  return 0;
}

void timeCreate(lomp::statistic * stat) {

  for (int i = 0; i < 5000; i++) {
    lomp::BlockTimer bt(stat);
    pthread_t thread;
    // Child thread needs to be created
    pthread_create(&thread, 0, doNothing, 0);
    pthread_join(thread, 0);
  }
}

int main(int, char **) {
  lomp::statistic stat;

  timeCreate(&stat);

  printf("Thread Create/Join Time\n"
         "pthread\n"
         "Time/thread\n");
  double tickInterval = lomp::tsc_tick_count::getTickTime();
  stat.scale(tickInterval);
  printf("Samples, Min, Mean, Max, SD\n%s\n", stat.format('s').c_str());
  return 0;
}
