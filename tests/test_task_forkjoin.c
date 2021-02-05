//===-- test_task_forkjoin.c - Test task execution at join ----------*- C++ -*-===//
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

#define DEBUG 0
#if (DEBUG)
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) (void)0
#endif

int test_omp_task(void) {
  int tids[NUM_TASKS];

#pragma omp parallel shared(tids)
  {
#pragma omp single nowait
    {
      for (int i = 0; i < NUM_TASKS; i++) {
        printf("Create task %d (&tids: %p, &i: %p)\n", i, &tids, &i);
#pragma omp task firstprivate(i), shared(tids)
        {
          int me = omp_get_thread_num();
          dprintf("%d: &tids: %p, &i: %p, i: %d\n", me, &tids, &i, i);
          if (!(0 <= i && i < NUM_TASKS)) {
            printf("%d: i (%d) out of range (0 <= i < %d)\n", me, i, NUM_TASKS);
          }
          dprintf("%d: i: %d\n", me, i);
          sleep(SLEEPTIME);
          if (!(0 <= i && i < NUM_TASKS)) {
            tids[i] = me;
          }
          printf("Executed task %d in thread %d\n", i, me);
        } /* end of omp task */
      }   /* end of for */
    }     /* end of single */
  }       /*end of parallel */

  /* Now we check that more than one thread executed the tasks. */
  for (int i = 1; i < NUM_TASKS; i++) {
    if (tids[0] != tids[i])
      return 1;
  }
  return 0;
}

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
