//===-- test_scheduling.cc - Test OpenMP loop scheduling  -*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains simple tests for OpenMP loop scheduling.
/// It chooses a variety of different loops (increasing and decreasing) with
/// strides, and then checks that all iterations are covered, and no iteration
/// is executed more than once.
///
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string>
#include <omp.h>
#include <unordered_map>

// A threadsafe way to remember information about the loop iterations.
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
static bool tracingEnabled = getenv("LOMP_TRACE");

static int check(std::unordered_map<uint32_t, uint32_t> & referenced, int base,
                 int end, int incr) {
  int failures = 0;

  if (incr > 0) {
    for (int i = base; i < end; i += incr) {
      auto executor = referenced.find(i);
      if (executor == referenced.end()) {
        fprintf(stderr, " [%d]:NOBODY, ", i);
        failures++;
        continue;
      }
      else if (debug) {
        fprintf(stderr, " [%d]:%d,", i, executor->second);
      }
      referenced.erase(i);
    }
  }
  else {
    for (int i = base; i > end; i += incr) {
      auto executor = referenced.find(i);
      if (executor == referenced.end()) {
        fprintf(stderr, "  index %d not executed\n", i);
        failures++;
        continue;
      }
      else if (debug) {
        fprintf(stderr, " [%d]:%d,", i, executor->second);
      }
      referenced.erase(i);
    }
  }
  if (debug) {
    fprintf(stderr, "\n");
  }
  // And check that there weren't any iterations executed which shouldn't have been!
  if (!referenced.empty()) {
    fprintf(stderr, "Extra iterations which should not have been executed:\n");
    for (const auto & n : referenced) {
      fprintf(stderr, "   %d executed by %d\n", n.first, n.second);
      failures++;
    }
  }
  return failures;
}

static bool runLoop(char const * name, omp_sched_t schedule, int base, int end,
                    int incr, int chunk) {
  lockedHash referenced;
  chunk = schedule != omp_sched_auto ? chunk : 0;
  omp_set_schedule(schedule, chunk);

  fprintf(stderr, "Testing schedule(%s,%d): for(i=%d; i%c%d; i += %d)\n", name,
          chunk, base, (incr < 0 ? '>' : '<'), end, incr);
  int failures = 0;
  auto numThreads = omp_get_max_threads();
  int counts[numThreads];
  for (auto i=0; i<numThreads; i++)
    counts[i] = 0;
  
#pragma omp parallel
  {
    int me = omp_get_thread_num();

    // fprintf(stderr, "%d/%d: in parallel region\n", me, omp_get_num_threads());
    if (incr > 0) {
#pragma omp for schedule(runtime)
      for (int i = base; i < end; i += incr) {
        if (debug)
          fprintf(stderr, "%d: i == %d\n", me, i);
	counts[me]++;
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
#pragma omp for schedule(runtime)
      for (int i = base; i > end; i += incr) {
        if (debug)
          fprintf(stderr, "%d: i == %d\n", me, i);
	counts[me]++;
        auto prev = referenced.lookup(i);
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
  fprintf(stderr, "schedule(%s,%d): for(i=%d; i%c%d; i += %d): %s\n", name,
          chunk, base, (incr < 0 ? '>' : '<'), end, incr,
          failures ? "***FAILED***" : "OK");

  printf ("Thread, Count\n");
  int total = 0;
  for (auto i=0; i<numThreads; i++) {
    total += counts[i];
  }
  for (auto i=0; i<numThreads; i++) {
    printf("  %4d,  %4d (%5.1f%%)\n", i, counts[i], (100.*counts[i])/total);
  }
  if (failures != 0 && tracingEnabled) {
    exit(-1);  // We want to get out soon so that the trace is not owverwritten.
    // The trace handler has an atexit() call so will print when we exit.
  }

  return failures != 0;
}

static struct {
  int base;
  int end;
  int incr;
  int chunk;
} loops[] = {
    {0, 20, 1, 1},    {0, 20, 2, 5},  {19, -1, -1, 1}, {0, 100, 1, 1},
    {999, -1, -1, 3}, {3, 100, 3, 5}, {1, 20, 1, 30},  {0, 2000, 1, 7},
};
enum { nLoops = sizeof(loops) / sizeof(loops[0]) };

static struct scheduleInfo {
  char const * name;
  omp_sched_t schedule;
} schedules[] = {
    {"auto", omp_sched_auto},
    {"static", omp_sched_static},
    {"monotonic:static", omp_sched_t(omp_sched_static | omp_sched_monotonic)},
    {"guided", omp_sched_guided},
    {"monotonic:guided", omp_sched_t(omp_sched_guided | omp_sched_monotonic)},
    {"dynamic", omp_sched_dynamic}, /* == nonmonotonic:dynamic */
    {"monotonic:dynamic", omp_sched_t(omp_sched_dynamic | omp_sched_monotonic)}, 
    /* hack for testing: all iterations allocated to thread zero, then behave as nonmnotonic */
    {"imbalanced", omp_sched_t(32)},
};
enum { nSchedules = sizeof(schedules) / sizeof(schedules[0]) };

static int testSchedule(scheduleInfo * sch) {
  int failures = 0;

  for (int i = 0; i < nLoops; i++) {
    auto loop = &loops[i];
    failures += runLoop(sch->name, sch->schedule, loop->base, loop->end,
			loop->incr, loop->chunk) ? 1:0;
  }

  return failures;
}

static scheduleInfo * findSchedule(char const * name) {
  std::string n(name);

  for (int s = 0; s < nSchedules; s++) {
    auto sch = &schedules[s];
    if (sch->name == n) {
      return sch;
    }
  }
  fprintf(stderr, "No schedule %s is known\n", name);
  return 0;
}

int main(int argc, char * argv[]) {
  int totalFailures = 0;
  int totalLoops = 0;

  fprintf(stderr, "Using %d threads\n", omp_get_max_threads());
  if (argc > 1) {
    scheduleInfo * sch = findSchedule(argv[1]);
    if (!sch) {
      return 1;
    }
    int iterations = 1;
    if (argc > 2) {
      iterations = atoi(argv[2]);
    }
    fprintf(stderr, "Running %d schedule(%s) loops\n", iterations, sch->name);
    int failures = 0;
    for (int i = 0; i < iterations; i++) {
      failures += testSchedule(sch);
      totalLoops += nLoops;
      totalFailures += failures;
    }
    fprintf(stderr, "%d of %d loops failed\n", failures, nLoops);
  }
  else {
    for (int s = 0; s < nSchedules; s++) {
      auto sch = &schedules[s];
      fprintf(stderr, "Running schedule(%s) loops\n", sch->name);
      int failures = testSchedule(sch);
      totalLoops += nLoops;
      totalFailures += failures;
      fprintf(stderr, "%d of %d loops failed\n", failures, nLoops);
    }
  }
  fprintf(stderr, "Total: %d of %d loops failed\n", totalFailures, totalLoops);
  return totalFailures;
}
