//===-- test_fork.c - Test fork call in isolation  ----------------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a unit test for the fork call without using OpenMP
/// just calling __kmpc_fork_call directly.
/// That may not, in general, be a reasonable thing to do, but during library
/// bootstrap it is useful
///
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdint.h>

#include "omp.h"

// The outlined parallel region body created by the compiler and passed to __kmpc_fork_call
typedef void (*BodyType)(void * GTid, void * LTid, ...);

extern void __kmpc_fork_call(void *, uint32_t, BodyType, ...);

static int failed = 0;
static int * mainArgs;

// Nine arguments tests stack argument passing on both X86_64 and AARCH64, since the maximum number
// of register arguments (on AARCH64) is eight.
void body9Args(void * GT, void * LT, int * a1, int * a2, int * a3, int * a4,
               int * a5, int * a6, int * a7) {
  if (omp_get_thread_num() != 0)
    return;

  int * argPointers[7];
  argPointers[0] = a1;
  argPointers[1] = a2;
  argPointers[2] = a3;
  argPointers[3] = a4;
  argPointers[4] = a5;
  argPointers[5] = a6;
  argPointers[6] = a7;

  printf("Thread %d of %d\n", omp_get_thread_num(), omp_get_num_threads());
#if (0)
  printf(" GT: %p, LT: %p, a1: %p, a2: %p, a3: %p, a4: %p, a5: %p, a6: %p, a7: "
         "%p\n",
         GT, LT, a1, a2, a3, a4, a5, a6, a7);
  fflush(stdout);
#endif

  fprintf(stderr, "     ArgPointers,      &mainArgs\n");
  for (int i = 0; i < 7; i++) {
    fprintf(stderr, "%16p, %16p %s\n", argPointers[i], &mainArgs[i],
            argPointers[i] != &mainArgs[i] ? "WRONG" : "");
    if (argPointers[i] != &mainArgs[i]) {
      failed = 1;
    }
  }
  if (failed)
    return;

  int argValues[7];
  argValues[0] = *a1;
  argValues[1] = *a2;
  argValues[2] = *a3;
  argValues[3] = *a4;
  argValues[4] = *a5;
  argValues[5] = *a6;
  argValues[6] = *a7;

  printf("In body9Args: GT %d, LT %d,"
         " %d,%d,%d,%d,%d,%d,%d\n",
         *(int *)GT, *(int *)LT, argValues[0], argValues[1], argValues[2],
         argValues[3], argValues[4], argValues[5], argValues[6]);

  for (int i = 0; i < 7; i++)
    if (argValues[i] != i + 1) {
      fprintf(stderr, "***ERROR*** arg %d is %d, not %d\n", i + 1, argValues[i],
              i + 1);
      // This could be a race if the fork call really goes parallel, but it should be safe even then,
      // since the only possibility is that another thread is also writing true to failed,
      // and our write gets lost, but it was redundant in that case anyway!
      failed = 1;
    }
}

int main(void) {
  int argValues[7] = {1, 2, 3, 4, 5, 6, 7};
  mainArgs = &argValues[0];

  __kmpc_fork_call(0, 7, (BodyType)body9Args, &argValues[0], &argValues[1],
                   &argValues[2], &argValues[3], &argValues[4], &argValues[5],
                   &argValues[6], &argValues[7]);

  printf("***%s***\n", failed ? "FAILED" : "PASSED");
  return failed;
}
