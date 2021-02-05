//===-- single_producer.c - Example with for a single producer  ---*- C -*-===//
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

    // slow down a little in producing the tasks, so that a worker
    // can steal the task from the queue
    sleep(1);
  }
}

int main(void) {
  double d = 42.0;
#pragma omp parallel
  {
#pragma omp master
    { produce(d); }
  }
  return 0;
}
