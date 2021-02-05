//===-- taskgroup.c - Example for the "taskgroup" construct -------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <unistd.h>

#include <omp.h>

#define NTASKS 32

void produce(double d) {
  for (int i = 0; i < NTASKS; i++) {
    // create a new task to for another thread to steal
    printf("%d: creating task\n", omp_get_thread_num());
#pragma omp task firstprivate(i) firstprivate(d)
    {
      double answer = i * d;
      printf("%d: Hello from task %d and the answer is %lf\n",
             omp_get_thread_num(), i, answer);
    }
  }
}

int main(void) {
  double d = 42.0;
#pragma omp parallel
  {
#pragma omp master
    {
#pragma omp taskgroup
      { produce(d); }
      printf("After the taskgroup\n");
    }
  }
  return 0;
}
