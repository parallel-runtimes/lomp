//===-- test_single.cc - Test OpenMP locks  -----------------------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains simple test for the omp single construct.
///
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdint.h>
#include <omp.h>
#include "../src/mlfsr32.h"
#include "../src/target.h"

int testSingle(void) {
  int count = 0;
  enum { NumLoops = 1000, MAX_THREADS = 512 };
  int numThreads = omp_get_max_threads();
  int threadCounts[MAX_THREADS];
  for (int i = 0; i < numThreads; i++) {
    threadCounts[i] = 0;
  }
#pragma omp parallel
  {
    lomp::randomDelay delay(0x7ff);
    int me = omp_get_thread_num();
    // Perverse dynamic scheduling!
    for (int i = 0; i < NumLoops; i++) {
#pragma omp single nowait
      {
#pragma omp atomic
        count++;
        threadCounts[me]++;
        // Introduce some short, but random, delay to allow multiple threads
        // the chance to get in an execute one of the single regions.
        delay.sleep();
      }
    }
  }

  if (count != NumLoops) {
    printf("***FAILED*** omp simgle nowait: saw %d iterations expected %d\n",
           count, NumLoops);
  }
  else {
    printf("omp simgle nowait passed: saw %d iterations expected %d\n", count,
           NumLoops);
  }
  printf("Thread, Singles executed\n");
  for (int i = 0; i < numThreads; i++) {
    printf("%4d, %6d\n", i, threadCounts[i]);
  }
  return count != NumLoops;
}

int main(void) {
  return testSingle();
}
