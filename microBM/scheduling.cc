//===-- scheduling.cc - Benchmark different schedules -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains code which contains nested loops
/// using different OpenMP schedules.

#include <omp.h>
#include <cstdint>
#include <math.h>
#include <complex>
#include "stats-timing.h"
#include "mlfsr32.h"

#define DIM 2500
uint64_t array[DIM][DIM];

static void initArray() {
  lomp::mlfsr32 rn(42);
  /* 42 is "the answer to life, the universe, and everything." */

  for (int i = 0; i < DIM; i++) {
    for (int j = 0; j < DIM; j++) {
      array[i][j] = rn.getNext() % 5000;
    }
  }
}

static inline void reference(uint64_t * d) {
  int * dp = (int *)d;
  __asm__ volatile("# Ensure compiler doesn't eliminate the referenced data"
                   :
                   : "r"(*dp));
}

// Ensure that the compiler sees us consuming the results of our calculations
static void referenceArray() {
  uint32_t rn = uint32_t(uintptr_t(&rn));
  auto a = &array[0][0];

  reference(&a[rn % (DIM * DIM)]);
}

// An arbitrary computation that we want to take constant, not completely
// negligible time.
static uint64_t loadFunction(uint64_t v) {
  for (int i = 0; i < 15; i++) {
    v = ((v + 4) * (v + 1)) / ((v + 2) * (v + 3));
  }
  return v;
}
//
// We do this rather than using schedule(runtime), because that
// forces the compiler to generate code compatible with a dynamic
// schedule (which requires a call into the runtime per chunk),
// whereas for a compiled-in static schedule it need only generate
// one call per thread per loop.
// Therefore schedule(runtime) OMP_SCHEDULE="static" has more overhead
// than schedule(static) in the code would.
//
// The work in here is completely arbitrary...
#define generateSquare(NAME, SCHEDULE, ARG1, ARG2)                             \
  static void square##NAME() {                                                 \
  _Pragma (STRINGIFY(omp for schedule(SCHEDULE)))       \
  for (int i=0; i<DIM; i++)                             \
      for (int j=0; j<DIM; j++)                         \
	array[i][j] = loadFunction(array[i][j]);                               \
  }

#define generateIncreasing(NAME, SCHEDULE, ARG1, ARG2)                         \
  static void increasing##NAME() {                                             \
  _Pragma (STRINGIFY(omp for schedule(SCHEDULE)))       \
  for (int i=0; i<DIM; i++)                             \
      for (int j=0; j<=i; j++)                          \
	array[i][j] = loadFunction(array[i][j]);                               \
  }

#define generateDecreasing(NAME, SCHEDULE, ARG1, ARG2)                         \
  static void decreasing##NAME() {                                             \
  _Pragma (STRINGIFY(omp for schedule(SCHEDULE)))       \
  for (int i=0; i<DIM; i++)                             \
      for (int j=i; j<DIM; j++)                         \
	array[i][j] = loadFunction(array[i][j]);                               \
  }

#define generateRandom(NAME, SCHEDULE, ARG1, ARG2)                             \
  static void random##NAME() {                                                 \
_Pragma (STRINGIFY(omp for collapse(2), schedule(SCHEDULE)))    \
  for (int i=0; i<DIM/2; i++)                                   \
    for (int j=0; j<DIM/2; j++) {                                              \
      int loops = int(array[i][j]) & 15;                                       \
      for (int k = 0; k <= loops; k++)                                         \
        array[i][j] = loadFunction(array[i][j]);                               \
    }                                                                          \
  }

// Sneaky, huh!
#define COMMA ,
// clang-format off
#define FOREACH_SCHEDULE(MACRO, ARG1, ARG2)                     \
  MACRO(static, static, ARG1, ARG2)                             \
  MACRO(static1, static COMMA 1, ARG1, ARG2)                    \
  MACRO(guided, guided, ARG1, ARG2)                             \
  MACRO(monotonic, monotonic : dynamic, ARG1, ARG2)             \
  MACRO(nonmonotonic, nonmonotonic : dynamic, ARG1, ARG2)

// Generate the functions
FOREACH_SCHEDULE(generateSquare, 0, 0)
FOREACH_SCHEDULE(generateIncreasing, 0, 0)
FOREACH_SCHEDULE(generateDecreasing, 0, 0)
FOREACH_SCHEDULE(generateRandom, 0, 0)

// Generate the lookup table
#define generateTableEntry(NAME, SCHEDULE, ARG1, ARG2)  \
  {STRINGIFY(ARG1##_##NAME), ARG1##NAME, ARG2},

static struct option {
  char const * name;
  void (*test)();
  int iterations;
} options[] = {
    FOREACH_SCHEDULE(generateTableEntry, square, DIM * DIM)
    FOREACH_SCHEDULE(generateTableEntry, increasing, DIM *(DIM + 1) / 2)
    FOREACH_SCHEDULE(generateTableEntry, decreasing, DIM *(DIM + 1) / 2)
    FOREACH_SCHEDULE(generateTableEntry, random, DIM * DIM / 4)
    // clang-format on
};

static option * findTest(char const * tag) {
  int const numTests = sizeof(options) / sizeof(options[0]);
  std::string arg(tag);

  for (int i = 0; i < numTests; i++) {
    auto opt = &options[i];
    if (arg == opt->name) {
      return opt;
    }
  }
  return 0;
}

static void help() {
  int const numTests = sizeof(options) / sizeof(options[0]);
  fprintf(stderr, "Possible options are ");
  for (int i = 0; i < numTests - 1; i++) {
    auto opt = &options[i];
    fprintf(stderr, "%s, ", opt->name);
  }
  fprintf(stderr, "%s\n", options[numTests - 1].name);
}

int main(int argc, char ** argv) {
  if (argc < 2) {
    fprintf(stderr, "An argument is required to choose the test\n");
    help();
    return 1;
  }
  auto opt = findTest(argv[1]);
  if (opt == 0) {
    fprintf(stderr, "%s is not a valid option\n", argv[1]);
    help();
    return 1;
  }

  fprintf(stderr, "\n");
  printf("# %d threads\n", omp_get_max_threads());

  lomp::statistic elapsed;
#pragma omp parallel
  {
    // Warm up...
    initArray();
    opt->test();

    int me = omp_get_thread_num();
    for (int i = 0; i < 100; i++) {
      if (me == 0) {
        initArray();
      }
#pragma omp barrier
      lomp::tsc_tick_count start;
      opt->test();
      lomp::tsc_tick_count end;
      if (me == 0) {
        elapsed.addSample((end - start).ticks());
        fprintf(stderr, ".");
        referenceArray();
      }
    }
  }
  fprintf(stderr, "\n");
  // Convert to seconds
  elapsed.scale(lomp::tsc_tick_count::getTickTime());
  // And scale down to a 1/throughput time
  elapsed.scaleDown(opt->iterations);
  printf("%d, %s\n", omp_get_max_threads(), elapsed.format('s').c_str());

  return 0;
}
