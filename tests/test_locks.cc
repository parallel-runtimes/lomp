//===-- test_locks.cc - Test OpenMP locks  ------------------------*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains simple tests for OpenMP locks.
/// To test all of the locks it needs to be run once for each lock type,
/// which can be chosen using the LOMP_LOCK_KIND envirable.
///
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdint.h>
#include <omp.h>
#include <atomic>
#include <cstdint>
#include "../src/target.h"

/// A 32 bit maximum length feedback shift register
class mlfsr32 {
  uint32_t State;

public:
  mlfsr32(uint32_t Initial = 0) {
    State = (Initial ? Initial : uint32_t(0xffffffff & uintptr_t(&Initial)));
    // Just in case the address is 4GiB aligned!
    // That is very unlikely since it's in the stack, but why not just fix it anyway?
    // Creation of the generator is not time critical.
    if (State == 0)
      State = 1;
  }

  uint32_t getNext() {
    // N.B. State can never be zero, since if it were the generator could never
    // escape from there.
    if (State & 1)
      State =
          (State >> 1) ^
          0x80000057; // Magic number from https://users.ece.cmu.edu/~koopman/lfsr/index.html
    else
      State = State >> 1;

    return State - 1; // So that we can return zero.
  }
};

static void runSanity() {
  enum { ITERATIONS = 10000 };
  std::atomic<int32_t> total;
  total = 0;
  auto numThreads = omp_get_max_threads();

  omp_lock_t l;
  omp_init_lock(&l);

#pragma omp parallel
  {
    mlfsr32 random;
    if (omp_get_num_threads() != numThreads) {
      printf("***BEWARE*** Only running with %d threads, not %d\n",
             omp_get_num_threads(), numThreads);
    }
    for (int i = 0; i < ITERATIONS; i++) {
      omp_set_lock(&l);
      int value = total;
      // Wait a random amount of time to give others the chance to interfere
      // if the locks aren't working!
      int sleeps = random.getNext() & 0xff;
      for (int i = 0; i < sleeps; i++)
        Target::Yield();
      total = value + 1;
      omp_unset_lock(&l);
    }
  }
  omp_destroy_lock(&l);

  printf("%d threads, counted %d which is %s\n", omp_get_max_threads(),
         int32_t(total),
         total == ITERATIONS * numThreads ? "correct" : "***INCORRECT***");
}

int main(void) {
  char const * lockName = getenv("LOMP_LOCK_KIND");
  if (lockName == 0)
    lockName = "Default";
  printf("%s: ", lockName);
  runSanity();
  return 0;
}
