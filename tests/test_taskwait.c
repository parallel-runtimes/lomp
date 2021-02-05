//===-- test_taskwait.cc - Test the "taskwait" construct ----------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file has been modified from the file
//    openmp/runtime/test/tasking/omp_taskwait.c
// of the LLVM project (https://github.com/llvm/llvm-project)
// under the Apache License v2.0 with LLVM Exceptions.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "omp.h"

#include "tests.h"

int test_omp_taskwait(void) {
  int result1 = 0; /* Stores number of not finished tasks after the taskwait */
  int result2 = 0; /* Stores number of wrong array elements at the end */
  int array[NUM_TASKS];
  int i;

  /* fill array */
  for (i = 0; i < NUM_TASKS; i++)
    array[i] = 0;

#pragma omp parallel
  {
#pragma omp single
    {
      for (i = 0; i < NUM_TASKS; i++) {
        /* First we have to store the value of the loop index in a new variable
         * which will be private for each task because otherwise it will be overwritten
         * if the execution of the task takes longer than the time which is needed to
         * enter the next step of the loop!
         */
        int myi;
        myi = i;
#pragma omp task
        {
          printf("Task %i sleeping in thread %d\n", myi, omp_get_thread_num());
          sleep(SLEEPTIME);
          array[myi] = 1;
        } /* end of omp task */
      }   /* end of for */
      printf("At taskwait construct\n");
#pragma omp taskwait
      /* check if all tasks were finished */
      for (i = 0; i < NUM_TASKS; i++)
        if (array[i] != 1)
          result1++;

      /* generate some more tasks which now shall overwrite
       * the values in the tids array */
      for (i = 0; i < NUM_TASKS; i++) {
        int myi;
        myi = i;
#pragma omp task
        {
          printf("Update task %d\n", myi);
          array[myi] = 2;
        } /* end of omp task */
      }   /* end of for */
    }     /* end of single */
  }       /*end of parallel */

  /* final check, if all array elements contain the right values: */
  for (i = 0; i < NUM_TASKS; i++) {
    if (array[i] != 2)
      result2++;
  }
  return ((result1 == 0) && (result2 == 0));
}

int main(void) {
  int num_failed = 0;

  if (!test_omp_taskwait()) {
    num_failed++;
  }

  return num_failed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
