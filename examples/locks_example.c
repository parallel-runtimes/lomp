//===-- locks_example.c - Example for lock usage ------------------*- C -*-===//
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

  omp_lock_t lock;
  omp_init_lock(&lock);

  printf("Before parallel region\n");
  printf("=======================================\n");
#pragma omp parallel shared(x, d, f) shared(lock)
  {
    omp_set_lock(&lock);
    printf("Hello World: ");
    printf("my secret is %lf ", d + f);
    printf("and %d\n", x);
    omp_unset_lock(&lock);
  }
  printf("=======================================\n");
  printf("After parallel region\n");

  omp_destroy_lock(&lock);

  return 0;
}
