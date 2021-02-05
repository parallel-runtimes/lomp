//===-- hello_world.c - Almost the classic "Hello World" example --*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>

#include <omp.h>

int main(void) {
  double d = 42.0;
  float f = 21.42;
  int x = 21;

  printf("Before parallel region\n");
  printf("=======================================\n");
#pragma omp parallel shared(x, d, f)
  {
    printf("Hello World: I am thread %d, and my secrets are %lf and %d\n",
           omp_get_thread_num(), (d + f) - 21.42, x);
  }
  printf("=======================================\n");
  printf("After parallel region\n");

  return 0;
}
