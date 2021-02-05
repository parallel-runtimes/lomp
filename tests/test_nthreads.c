//===-- test_nthreads.cc - Test setting the number of threads  ----*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

#include <omp.h>

#define NTHREADS_MAX 128

int main(void) {
  int failed = 0;

  // By doing this we can check whether setting the number of threads to what it already is
  // is accepted, even when a real change is not.
  // (If we see a failure when "changing" from 1 to 1, that's wrong, while
  // changing from 1 to 2 should be rejected).
  omp_set_num_threads(1);

  for (int i = 1; i <= NTHREADS_MAX; ++i) {
#pragma omp parallel num_threads(i)
    {
#pragma omp single
      {
        int nthreads = omp_get_num_threads();
        if (nthreads != i) {
          printf("Got %d threads, should have been %d\n", nthreads, i);
          fflush(stdout);
          failed++;
        }
      }
    }
  }

  printf("***%s***\n", failed ? "FAILED" : "PASSED");
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
