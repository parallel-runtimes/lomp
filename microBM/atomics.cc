//===-- atomics.cc - Measure atomic operation timing ------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains micro-benchmarks to measure the time taken by atomic operations.

#include <omp.h>
#include <atomic>
#include <cstdint>
#include <ctime>

#include "stats-timing.h"
#include "target.h"
#include "atomics_impl.h"
#include "mlfsr32.h"

// We use OpenMP to set up and bind threads, but our measurements here are of hardware properties,
// not those of the OpenMP runtime which is being used.

// Note that the use of alignas here is not guaranteed to work, since
// the C++ standard does not require alignment at such large
// granularity. However, it does work on the compilers and machines in
// which we are interested, and we check that it has worked.

#if (LOMP_TARGET_LINUX)
// Force thread affinity.
// This is a dubious thing to do; it'd be better to use the functionality
// of the OpenMP runtime, but this is simple, at least.
#include <sched.h>
void forceAffinity() {
  int me = omp_get_thread_num();
  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(me, &set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0)
    fprintf(stderr, "Failed to force affinity for thread %d\n", me);
  //  else
  //    fprintf(stderr,"Thread %d bound to cpuid bit %d\n",me,me);
}
#else
void forceAffinity() {}
#endif

#define MAX_THREADS 256
#define NumSamples 1000
#define InnerReps 1000

// Provide a random exponential backoff.
// We use the CPU "cycle" count timer to provide
// delay between around 100ns and 25us.
class randomExponentialBackoff {
  const float smallestTime = 100.e-9; // 100ns
  // multiplier used to convert our units into timer ticks
  static uint32_t timeFactor;

  lomp::mlfsr32 random;
  uint32_t mask;          // Limits current delay
  enum { maxMask = 255 }; // 256*100ns = 25.6us
  uint32_t sleepCount;    // How many times has sleep been called
  uint32_t delayCount;    // Only needed for stats

  enum {
    initialMask = 1,
    delayMask = 1, // Do two delays at each exponential value
  };

public:
  randomExponentialBackoff() : mask(1), sleepCount(0), delayCount(0) {
    // Racy, but it doesn't matter since everyone will set the
    // same value.
    if (timeFactor == 0)
      timeFactor = smallestTime / lomp::tsc_tick_count::getTickTime();
  }
  void sleep() {
    uint32_t count = 1 + (random.getNext() & mask);
    delayCount += count;
    lomp::tsc_tick_count end =
        lomp::tsc_tick_count::now().getValue() + delayCount * timeFactor;
    // Up to next power of two if it's time to ramp.
    if ((++sleepCount & delayMask) == 0)
      mask = ((mask << 1) | 1) & maxMask;
    // And delay
    while (lomp::tsc_tick_count::now().before(end))
      Target::Yield();
  }
  uint32_t getDelayCount() const {
    return delayCount;
  }
};

uint32_t randomExponentialBackoff::timeFactor = 0;

// A version of FP atomic plus with random exponential backoff
void atomicPlusRE(float * target, float operand) {
  std::atomic<uint32_t> * t = (std::atomic<uint32_t> *)target;
  typedef union {
    uint32_t uintValue;
    float typeValue;
  } sharedBits;

  sharedBits current;
  sharedBits next;

  // Try once before setting up backoff structures and so on
  current.uintValue = *t;
  next.typeValue = current.typeValue + operand;
  if (t->compare_exchange_strong(current.uintValue, next.uintValue))
    return;
  // Try again immediately since we just read the value and have it in
  // exclusive state so this is almost free
  next.typeValue = current.typeValue + operand;
  if (t->compare_exchange_strong(current.uintValue, next.uintValue))
    return;

  // Ok, there is contention, set up backoff
  randomExponentialBackoff backoff;
  for (;;) {
    backoff.sleep();
    current.uintValue = *t;
    next.typeValue = current.typeValue + operand;
    if (t->compare_exchange_strong(current.uintValue, next.uintValue))
      return;
    // Try again immediately for the same reason as above.
    next.typeValue = current.typeValue + operand;
    if (t->compare_exchange_strong(current.uintValue, next.uintValue))
      return;
  }
}

// A version of FP atomic plus with no backoff, but test and test&set
void atomicPlusTTAS(float * target, float operand) {
  std::atomic<uint32_t> * t = (std::atomic<uint32_t> *)target;
  typedef union {
    uint32_t uintValue;
    float typeValue;
  } sharedBits;

  for (;;) {
    sharedBits current;
    sharedBits next;

    current.uintValue = *t;
    next.typeValue = current.typeValue + operand;
    if (*t == current.uintValue) {
      if (t->compare_exchange_strong(current.uintValue, next.uintValue))
        return;
      // Try again immediately since we just read the value and have it in
      // exclusive state so this is almost free
      next.typeValue = current.typeValue + operand;
      if (*t == current.uintValue &&
          t->compare_exchange_strong(current.uintValue, next.uintValue))
        return;
    }
    Target::Yield();
  }
}

// A version of FP atomic plus with random exponential backoff where we
// collect backoff statistics.
void atomicPlusRECount(lomp::statistic * stat, float * target, float operand) {
  std::atomic<uint32_t> * t = (std::atomic<uint32_t> *)target;
  typedef union {
    uint32_t uintValue;
    float typeValue;
  } sharedBits;

  sharedBits current;
  sharedBits next;

  // Try once before setting up backoff structures and so on
  current.uintValue = *t;
  next.typeValue = current.typeValue + operand;
  if (t->compare_exchange_strong(current.uintValue, next.uintValue)) {
    stat->addSample(0);
    return;
  }

  // Try again immediately since we just read the value and have it in
  // exclusive state so this is almost free
  next.typeValue = current.typeValue + operand;
  if (t->compare_exchange_strong(current.uintValue, next.uintValue)) {
    stat->addSample(0);
    return;
  }

  // Ok, there is contention, set up backoff
  randomExponentialBackoff backoff;
  for (;;) {
    backoff.sleep();
    current.uintValue = *t;
    next.typeValue = current.typeValue + operand;
    if (t->compare_exchange_strong(current.uintValue, next.uintValue)) {
      stat->addSample(backoff.getDelayCount());
      return;
    }
    // Try again immediately for the same reason as above.
    next.typeValue = current.typeValue + operand;
    if (t->compare_exchange_strong(current.uintValue, next.uintValue)) {
      stat->addSample(backoff.getDelayCount());
      return;
    }
  }
}

typedef void (*Operation)(void * target);

static void doIntegerIncrement(void * t) {
  std::atomic<uint32_t> * target = (std::atomic<uint32_t> *)t;

  for (int i = 0; i < InnerReps; i++)
    (*target)++;
}

// Use default FP op with no backoff from atomics_impl.h
static void doFPIncrement(void * t) {
  float * target = (float *)t;

  for (int i = 0; i < InnerReps; i++)
    atomicPlus(target, 1.0f);
}

// Use TTAS style
static void doFPIncrementTTAS(void * t) {
  float * target = (float *)t;

  for (int i = 0; i < InnerReps; i++)
    atomicPlusTTAS(target, 1.0f);
}

// Use our hacked version with backoff.
static void doFPIncrementRE(void * t) {
  float * target = (float *)t;

  for (int i = 0; i < InnerReps; i++)
    atomicPlusRE(target, 1.0f);
}

struct alignedUint32 {
  CACHE_ALIGNED uint32_t value;
  alignedUint32() : value(0) {}
  operator uint32_t() const {
    return value;
  }
  alignedUint32 & operator=(uint32_t v) {
    value = v;
    return *this;
  }
};

void measureAtomic(lomp::statistic * stats, Operation op) {
  int nThreads = omp_get_max_threads();

  // Dubious use of casting in here, and the assumption that 0.0 has the all zero bits represenation,
  // but we should get away with it!
  alignedUint32 value;
  if ((uintptr_t(&value) & (CACHELINE_SIZE - 1)) != 0) {
    fprintf(stderr, "Test value is not cache line aligned: addr %p\n", &value);
    return;
  }
  // Range has already been checked in main.
  lomp::statistic threadStats[MAX_THREADS];

#pragma omp parallel
  {
    int me = omp_get_thread_num();

    for (int count = 1; count <= nThreads; count++) {
      int iterations = NumSamples / count;
      if (me < count) {
        for (int i = 0; i <= iterations; i++) {
          lomp::BlockTimer bt(&threadStats[me]);
          op(&value);
        }
        threadStats[me].scaleDown(InnerReps);
      }
#pragma omp barrier
      // Accumulate all the thread stats into the value for the number of participants
      // and reset them ready for the next measurement.
      if (me == 0) {
        fprintf(stderr, ".");
        for (int i = 0; i < count; i++) {
          stats[count - 1] += threadStats[i];
          threadStats[i].reset();
        }
        uint32_t expectedValue = count * InnerReps * (iterations + 1);
        if (value != expectedValue &&
            *(float *)&value != float(expectedValue)) {
          fprintf(stderr, "***Dubious validation: %u expected %u (or %f, %f)\n",
                  uint32_t(value), expectedValue, *(float *)&value,
                  float(expectedValue));
        }
        value = 0;
      }
#pragma omp barrier
    }
  }

  fprintf(stderr, "\n");
}

// Measure the backoff counts.
void measureBackoff(lomp::statistic * stats) {
  int nThreads = omp_get_max_threads();

  // Dubious use of casting in here, and the assumption that 0.0 has the all zero bits represenation,
  // but we should get away with it!
  alignedUint32 value;
  if ((uintptr_t(&value) & (CACHELINE_SIZE - 1)) != 0) {
    fprintf(stderr, "Test value is not cache line aligned: addr %p\n", &value);
    return;
  }
  // Range has already been checked in main.
  lomp::statistic threadStats[MAX_THREADS];
  for (int i = 0; i < MAX_THREADS; i++)
    threadStats[i].collectHist();

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    lomp::statistic * myStat = &threadStats[me];

    for (int count = 1; count <= nThreads; count++) {
      int iterations = NumSamples / count;
      if (me < count) {
        for (int i = 0; i <= iterations; i++)
          for (int j = 0; j < InnerReps; j++)
            atomicPlusRECount(myStat, (float *)&value, 1.0f);
      }
#pragma omp barrier
      // Accumulate all the thread stats into the value for the number of participants
      // and reset them ready for the next measurement.
      if (me == 0) {
        fprintf(stderr, ".");
        for (int i = 0; i < count; i++) {
          stats[count - 1] += threadStats[i];
          threadStats[i].reset();
        }
        uint32_t expectedValue = count * InnerReps * (iterations + 1);
        if (value != expectedValue &&
            *(float *)&value != float(expectedValue)) {
          fprintf(stderr, "***Dubious validation: %u expected %u (or %f, %f)\n",
                  uint32_t(value), expectedValue, *(float *)&value,
                  float(expectedValue));
        }
        value = 0;
      }
#pragma omp barrier
    }
  }

  fprintf(stderr, "\n");
}

static void printHelp() {
  printf("The first argument determines the test.\n"
         "I[ifet]      -- Atomic increment of integer (i) or float (f)\n"
         "                e = float with random e**x backoff\n"
         "                t = float with TTAS\n"
         "B            -- Backoff stats for fp add\n");
}

static void printStats(lomp::statistic * stats, int count, int offset = 1) {
  // Convert to times
  double tickInterval = lomp::tsc_tick_count::getTickTime();
  for (int i = 0; i < count; i++)
    stats[i].scale(tickInterval);

  for (int i = 0; i < count; i++)
    printf("%6d, %s\n", i + offset, stats[i].format('s').c_str());
}

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

static struct opDesc {
  char tag;
  void (*op)(void *);
  char const * name;
} operations[] = {{'i', doIntegerIncrement, "uint32_t (std::atomic)"},
                  {'f', doFPIncrement, "float (no backoff)"},
                  {'e', doFPIncrementRE, "float (random e**x backoff)"},
                  {'t', doFPIncrementTTAS, "float (TTAS)"}};

struct opDesc * findOp(char const tag) {
  for (auto i = 0u; i < sizeof(operations) / sizeof(operations[0]); i++)
    if (operations[i].tag == tag)
      return &operations[i];
  return 0;
}

int main(int argc, char ** argv) {
  int nThreads = omp_get_max_threads();

  if (nThreads > MAX_THREADS) {
    printf("%d threads available, increase MAX_THREADS (%d)\n", nThreads,
           MAX_THREADS);
    return 1;
  }

  if (argc < 2) {
    printf("Need an argument\n");
    printHelp();
    return 1;
  }

  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

// Warm up...
#pragma omp parallel
  { forceAffinity(); }

  // Most of the tests are per-thread
  fprintf(stderr, "%s %s\n", argv[0], argv[1]);

  switch (argv[1][0]) {
  case 'I': {
    struct opDesc * op = findOp(argv[1][1]);
    if (!op) {
      fprintf(stderr, "Failed to find experiment\n");
      printHelp();
      return 1;
    }
    lomp::statistic threadCountStats[MAX_THREADS];
    lomp::statistic * stats = &threadCountStats[0];

    measureAtomic(stats, op->op);
    printf("Atomic Increment\n"
           "%s, %s\n"
           "# %s"
           "# %s\n"
           "# %s\n\n"
           "Threads,  Count,       Min,      Mean,       Max,        SD\n",
           targetName.c_str(), op->name, getDateTime().c_str(),
           lomp::tsc_tick_count::timerDescription().c_str(), COMPILER_NAME);
    printStats(&stats[0], nThreads, 1);
  } break;

  case 'B': {
    // Backoff stats
    lomp::statistic threadCountStats[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++)
      threadCountStats[i].collectHist();
    lomp::statistic * stats = &threadCountStats[0];

    measureBackoff(stats);
    printf("Backoff count\n"
           "%s\n"
           "# %s"
           "# %s\n"
           "# %s\n\n"
           "Threads,  Count,       Min,      Mean,       Max,        SD\n",
           targetName.c_str(), getDateTime().c_str(),
           lomp::tsc_tick_count::timerDescription().c_str(), COMPILER_NAME);
    for (int i = 0; i < nThreads; i++)
      printf("%6d, %s\n", i + 1, stats[i].format(' ').c_str());

    // And print histograms
    for (int i = 0; i < nThreads; i++) {
      printf("\nLog histogram for %d threads\n", i + 1);
      printf("%s\n", threadCountStats[i].getHist()->format(' ').c_str());
    }
    break;
  }
  default:
    printf("Unknown experiment\n");
    printHelp();
    return 1;
  }

  return 0;
}
