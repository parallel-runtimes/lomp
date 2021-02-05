//===-- task_hello.c - Example for the "task" construct -----------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <unistd.h>

#include <omp.h>

#define NTASKS 16
#define MANY_TASKS 1

void create_task(int i, double d) {
#pragma omp task firstprivate(i) firstprivate(d)
  {
    double answer = i * d;
#if MANY_TASKS
#pragma omp task firstprivate(answer) firstprivate(i)
#endif
    {
      printf("Hello from task %d/1 on thread %d, and the answer is %lf (%lf x "
             "%d)\n",
             i, omp_get_thread_num(), answer, answer / i, i);
    }
#if MANY_TASKS
#pragma omp task firstprivate(answer) firstprivate(i)
    {
      printf("Hello from task %d/2 on thread %d, and the answer is %lf (%lf x "
             "%d)\n",
             i, omp_get_thread_num(), answer, answer / i, i);
    }
#endif
  }
}

int main(void) {
  double d = 42.0;

#pragma omp parallel
  {
#pragma omp master
    for (int i = 0; i < NTASKS; ++i) {
      create_task(i + 1, d);
    }
  }
  return 0;
}
