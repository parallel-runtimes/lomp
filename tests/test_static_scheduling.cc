//===-- test_static_scheduling.c - Test OpenMP static scheduling  -*- C -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains simple tests for OpenMP static scheduling.
/// It chooses a variety of different loops (increasing and decreasing) with
/// strides, and then checks that all iterations are covered, and no iteration
/// is executed more than once.
///
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <omp.h>
#include <unordered_map>

class lockedHash {
  std::unordered_map<uint32_t, uint32_t> theMap;
  omp_lock_t theLock;

public:
  lockedHash() {
    omp_init_lock(&theLock);
  }
  void insert(uint32_t key, uint32_t value) {
    omp_set_lock(&theLock); // Claim the lock
    theMap.insert({key, value});
    omp_unset_lock(&theLock); // Release the lock
  }
  uint32_t lookup(uint32_t key) {
    omp_set_lock(&theLock); // Claim the lock
    auto result = theMap.find(key);
    omp_unset_lock(&theLock); // Release the lock
    return result == theMap.end() ? -1 : result->second;
  }
  std::unordered_map<uint32_t, uint32_t> & getMap() {
    return theMap;
  }
};

static bool debug = getenv("LOMP_DEBUG");

static int check(std::unordered_map<uint32_t, uint32_t> & referenced, int base,
                 int end, int incr) {
  int failures = 0;

  if (incr > 0) {
    if (debug) {
      for (int i = base; i < end; i += incr) {
        auto executor = referenced.find(i)->second;
        fprintf(stderr, " [%d]:%d,", i, executor);
      }
    }
    for (int i = base; i < end; i += incr) {
      if (referenced.find(i) == referenced.end()) {
        fprintf(stderr, "  index %d not executed\n", i);
        failures++;
        continue;
      }
      referenced.erase(i);
    }
  }
  else {
    if (debug) {
      for (int i = base; i > end; i += incr) {
        auto executor = referenced.find(i)->second;
        fprintf(stderr, " [%d]:%d,", i, executor);
      }
    }
    for (int i = base; i > end; i += incr) {
      if (referenced.find(i) == referenced.end()) {
        fprintf(stderr, "  index %d not executed\n", i);
        failures++;
        continue;
      }
      referenced.erase(i);
    }
  }
  if (!referenced.empty()) {
    fprintf(stderr, "Extra iterations which shoould not have been executed:\n");
    for (const auto & n : referenced) {
      fprintf(stderr, "   %d executed by %d\n", n.first, n.second);
      failures++;
    }
  }
  return failures;
}

static bool runSimple(int base, int end, int incr) {
  // Try a variety of different schedule with the default static schedule
  lockedHash referenced;

  fprintf(stderr, "Testing i=%d; i%c%d i += %d [no schedule]\n", base,
          (incr < 0 ? '>' : '<'), end, incr);
  int failures = 0;
#pragma omp parallel
  {
    int me = omp_get_thread_num();
    // fprintf(stderr, "%d/%d: in parallel region\n", me, omp_get_num_threads());
    if (incr > 0) {
#pragma omp for
      for (int i = base; i < end; i += incr) {
        // fprintf(stderr, "%d: i == %d\n", me, i);
        int prev = referenced.lookup(i);
        if (prev != -1) {
          fprintf(stderr, "  index %d executed by %d AND %d\n", i, prev, me);
#pragma omp atomic
          failures++;
        }
        referenced.insert(i, me);
      }
    }
    else {
#pragma omp for
      for (int i = base; i > end; i += incr) {
        // fprintf(stderr, "%d: i == %d\n", me, i);
        int prev = referenced.lookup(i);
        if (prev != -1) {
          fprintf(stderr, "  index %d executed by %d AND %d\n", i, prev, me);
#pragma omp atomic
          failures++;
        }
        referenced.insert(i, me);
      }
    }
  }
  failures += check(referenced.getMap(), base, end, incr);
  fprintf(stderr, "  %s\n", failures ? "***FAILED***" : "OK");
  return failures != 0;
}

static bool runChunked(int base, int end, int incr, int chunk) {
  // Try a variety of different schedule with the default static schedule
  lockedHash referenced;
  fprintf(stderr, "Testing i=%d; i%c%d i += %d schedule(static,%d)\n", base,
          (incr < 0 ? '>' : '<'), end, incr, chunk);
  int failures = 0;
#pragma omp parallel
  {
    int me = omp_get_thread_num();
    // fprintf(stderr, "%d/%d: in parallel region\n", me, omp_get_num_threads());
    if (incr > 0) {
#pragma omp for schedule(static, chunk)
      for (int i = base; i < end; i += incr) {
        // fprintf(stderr, "%d: i == %d\n", me, i);
        int prev = referenced.lookup(i);
        if (prev != -1) {
          fprintf(stderr, "  index %d executed by %d AND %d\n", i, prev, me);
#pragma omp atomic
          failures++;
        }
        referenced.insert(i, me);
      }
    }
    else {
#pragma omp for schedule(static, chunk)
      for (int i = base; i > end; i += incr) {
        // fprintf(stderr, "%d: i == %d\n", me, i);
        int prev = referenced.lookup(i);
        if (prev != -1) {
          fprintf(stderr, "  index %d executed by %d AND %d\n", i, prev, me);
#pragma omp atomic
          failures++;
        }
        referenced.insert(i, me);
      }
    }
  }
  failures += check(referenced.getMap(), base, end, incr);

  fprintf(stderr, "  %s\n", failures ? "***FAILED***" : "OK");
  return failures != 0;
}

static struct {
  int base;
  int end;
  int incr;
  int chunk;
} loops[] = {{0, 20, 1, 1},    {0, 20, 2, 5},  {19, -1, -1, 1}, {0, 100, 1, 1},
             {999, -1, -1, 3}, {3, 100, 3, 5}, {1, 20, 1, 30}};

int main(void) {
  int blockedFailures = 0;
  int cyclicFailures = 0;
  int nLoops = sizeof(loops) / sizeof(loops[0]);

  fprintf(stderr, "Static loop scheduling on %d threads\n",
          omp_get_max_threads());
  fprintf(stderr, "Running schedule(static) loops\n");
  for (int i = 0; i < nLoops; i++) {
    auto loop = &loops[i];
    blockedFailures += runSimple(loop->base, loop->end, loop->incr) ? 1 : 0;
  }
  fprintf(stderr, "%d of %d loops failed\n", blockedFailures, nLoops);

  fprintf(stderr, "Running schedule(static,n) loops\n");
  for (int i = 0; i < nLoops; i++) {
    auto loop = &loops[i];
    cyclicFailures +=
        runChunked(loop->base, loop->end, loop->incr, loop->chunk) ? 1 : 0;
  }

  fprintf(stderr, "%d of %d loops failed\n", cyclicFailures, nLoops);
  return blockedFailures + cyclicFailures;
}
