//===-- task_barrier.c - Example for tasks and "barrier"s ---------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <unistd.h>

#include <omp.h>

void before_barrier() {
  int tid = omp_get_thread_num();
#pragma omp task firstprivate(tid)
  {
    printf("before barrier, task A/%d on thread %d\n", tid,
           omp_get_thread_num());
  }
}

void after_barrier() {
  int tid = omp_get_thread_num();
#pragma omp task firstprivate(tid)
  {
    printf("after barrier, task B/%d on thread %d\n", tid,
           omp_get_thread_num());
  }
}

int main(void) {
#pragma omp parallel
  {
    before_barrier();
#pragma omp barrier
    after_barrier();
  }
  return 0;
}
