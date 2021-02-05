//===-- test_task_barrier.cc - Test task execution at barriers  ---*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file has been modified from the file
//    openmp/runtime/test/tasking/omp_task.c
// of the LLVM project (https://github.com/llvm/llvm-project)
// under the Apache License v2.0 with LLVM Exceptions.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <omp.h>

#include "tests.h"

int test_omp_task(void) {
  int tids[NUM_TASKS];
  int i;

#pragma omp parallel
  {
#pragma omp single nowait
    {
      for (i = 0; i < NUM_TASKS; i++) {
        /* First we have to store the value of the loop index in a new variable
         * which will be private for each task because otherwise it will be overwritten
         * if the execution of the task takes longer than the time which is needed to
         * enter the next step of the loop!
         */
        int myi;
        myi = i;
        printf("Create task %d\n", myi);
#pragma omp task
        {
          sleep(SLEEPTIME);
          tids[myi] = omp_get_thread_num();
          printf("Executed task %d in thread %d\n", myi, omp_get_thread_num());
        } /* end of omp task */
      }   /* end of for */
    }     /* end of single */
    printf("Before barrier\n");
#pragma omp barrier
    printf("After barrier\n");
  } /*end of parallel */

  /* Now we check if more than one thread executed the tasks. */
  for (i = 1; i < NUM_TASKS; i++) {
    if (tids[0] != tids[i])
      return 1;
  }
  return 0;
} /* end of check_parallel_for_private */

int main(void) {
  int i;
  int num_failed = 0;

  if (omp_get_max_threads() < 2) {
    printf("Not enough threads for this test!  Need >2 threads!\n");
  }
  //   omp_set_num_threads(8);

  for (i = 0; i < REPETITIONS; i++) {
    if (!test_omp_task()) {
      num_failed++;
    }
  }

  return num_failed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
