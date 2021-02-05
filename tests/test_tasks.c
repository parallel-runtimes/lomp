//===-- test_tasks.c - Test task creation and execution  ----------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

#define NTASKS 33

int main(void) {
  int count = 0;

#pragma omp parallel shared(count)
#pragma omp master
  {
    for (int i = 0; i < NTASKS; i++) {
#pragma omp task shared(count)
      {
#pragma omp atomic
        count++;
      }
    }
  }

  int failed = (count != NTASKS);
  printf("Got %d tasks, should be %d\n"
         "***%s***\n",
         count, NTASKS, failed ? "FAILED" : "PASSED");
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
  ;
}
