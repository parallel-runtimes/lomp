//===-- test_critical.c - Test the "critical" construct -----------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <omp.h>

#include "tests.h"

int main(void) {
  int num_failed = 0;
  int in_critical = 0;

#pragma omp parallel
  {
#pragma omp critical
    {
      int locked;

// Determine if critical section is currently held by another thread
#pragma omp atomic read
      locked = in_critical;

      if (locked) {
        // This should not happen, so count this as an error
#pragma omp atomic
        num_failed++;
      }
      else {
        // This is the correct behavior, so flag this region as being
        // executed by the current thread.
#pragma omp atomic write
        in_critical = 1;
      }

      // Wait a bit before releasing the critical region again.
      printf("Thread %d: in critical region\n", omp_get_thread_num());
      sleep(SLEEPTIME);

#pragma omp atomic write
      in_critical = 0;
    }
  }

  return num_failed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}