//===-- life.c - Parallel region printing a message ---------------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <omp.h>

void life(void) {
  float value = 21.0f;
  int factor = 2;
#pragma omp parallel shared(value) firstprivate(factor)
  {
    int thread_id = omp_get_thread_num();
    printf("Thread %d says: the meaning of life is %f\n", thread_id,
           value * factor);
  }
}

int main(void) {
  life();
  return 0;
}
