//===-- fib.c - The Fibonacci numbers  ----------------------------*- C -*-===//
//
// Part of the LOMP Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include <omp.h>

#define CUTOFF 1

size_t fib_seq(size_t n) {
  if (n < 2)
    return n;
  return fib_seq(n - 1) + fib_seq(n - 2);
}

size_t fib_task(size_t n) {
  size_t x, y;

  if (n < 2)
    return n;

#if CUTOFF
  if (n < 10) {
    x = fib_seq(n - 1);
    y = fib_seq(n - 2);
  }
  else {
#endif
#pragma omp task shared(x)
    x = fib_task(n - 1);

#pragma omp task shared(y)
    y = fib_task(n - 2);

#pragma omp taskwait
#if CUTOFF
  }
#endif
  return x + y;
}

size_t fib_par(size_t n) {
  size_t fib = 0;
#pragma omp parallel
  {
    if (0 == omp_get_thread_num()) {
#pragma omp task firstprivate(n) shared(fib)
      fib = fib_task(n);
    }
  }
  return fib;
}

#define N 10

int main(int argc, char * argv[]) {
  (void)argv;
  long int n = N;
  char * endptr;

  if (argc > 1) {
    n = strtol(argv[1], &endptr, 10);
    if ((errno == ERANGE && (n == LONG_MAX || n == LONG_MIN)) ||
        (errno != 0 && n == 0)) {
      n = N;
    }
  }

  printf("fib(%zu) [sequential] = %zu\n", n, fib_seq(n));
  printf("fib(%zu) [parallel]   = %zu\n", n, fib_par(n));
  return 0;
}
