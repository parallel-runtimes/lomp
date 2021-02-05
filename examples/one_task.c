//===-- one_task.c - Creating exactly on task ---------------------*- C -*-===//
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

void spawn(int i, double d) {
#pragma omp task firstprivate(i) firstprivate(d)
  {
    double answer = i * d;
    printf("%d: Hello from task; the answer is %lf\n", omp_get_thread_num(),
           answer);
  }
}

void consume() {
  // This function is only a placeholder to mark a worker thread
  // as being a consumer.  The actual task execution is done in
  // the barrier of the parallel region.
}

int main(void) {
  double d = 21.0;
  int i = 2;
#pragma omp parallel
  {
#pragma omp master
    spawn(i, d);
  }
  return 0;
}
