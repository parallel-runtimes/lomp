//===-- test_taskargs.c - Test task creation and argument passing  *- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "omp.h"

int main(void) {
  int failed = 0;
#pragma omp parallel shared(failed)
  {
#pragma omp master
    {
      int tpvar = 42;
      int tpvar2 = 84;

#pragma omp task firstprivate(tpvar, tpvar2)
      {
        int me = omp_get_thread_num();

        fprintf(stderr, "In task in thread %d\n", me);
        fflush(stderr);
        fprintf(stderr, "%d: &tpvar = %p, &tpvar2 = %p\n", me, &tpvar, &tpvar2);
        fflush(stderr);
        fprintf(stderr,
                "%d: tpvar = %d (should be 42), tpvar2 = %d "
                "(should be 84)\n",
                me, tpvar, tpvar2);
        failed = (tpvar != 42) || (tpvar2 != 84);
        fflush(stderr);
      }
    }
  }
  printf("***%s***\n", failed ? "FAILED" : "PASSED");

  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
