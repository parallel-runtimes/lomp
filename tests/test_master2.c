//===-- test_master2.cc - Test the "master" construct ------=------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file has been modified from the file
//    openmp/runtime/test/master/omp_master_3.c
// of the LLVM project (https://github.com/llvm/llvm-project)
// under the Apache License v2.0 with LLVM Exceptions.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>

#include <omp.h>

#include "tests.h"

int test_omp_master_3(void) {
  int nthreads;
  int executing_thread;
  int tid_result = 0; /* counts up the number of wrong thread no. for
               the master thread. (Must be 0) */
  nthreads = 0;
  executing_thread = -1;

#pragma omp parallel
  {
#pragma omp master
    {
      int tid = omp_get_thread_num();
      if (tid != 0) {
#pragma omp atomic
        tid_result++;
      }
#pragma omp atomic
      nthreads++;
      executing_thread = omp_get_thread_num();
    } /* end of master*/
  }   /* end of parallel*/

  return ((nthreads == 1) && (executing_thread == 0) && (tid_result == 0));
}

int main(void) {
  int i;
  int num_failed = 0;

  for (i = 0; i < REPETITIONS; i++) {
    if (!test_omp_master_3()) {
      num_failed++;
    }
  }

  return num_failed != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
