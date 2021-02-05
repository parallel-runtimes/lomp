//===-- barriers.cc - Measure barrier operation timing ----------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains micro-benchmarks to measure the time taken by different barriers.

#include <omp.h>
#include <atomic>
#include <cstdint>
#include <ctime>
// algorithm just for std::max
#include <algorithm>

#include "stats-timing.h"
#include "target.h"
#include "channel.h"
#include "mlfsr32.h"

// Use the same interface for barriers as the runtime does, so that we
// can use its implementations.
#include "barriers.h"

// Provide the passthrough function without calling into the tasking system.
bool lomp::Barrier::checkIn(int me, bool) {
  // Then invoke the virtual method to call whichever implementation we're using.
  return checkIn(me);
}

//
// Compute the offset between thread zero's clock and that in each
// of the other threads.
//
static void computeClockOffset(int64_t * offsets) {
  int nThreads = omp_get_max_threads();
  channel<int64_t> zeroToOther;
  channel<int64_t> otherToZero;
  int const NumTests = 10000;
  offsets[0] = 0;

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    for (int other = 1; other < nThreads; other++) {
      if (me == 0) {
        lomp::statistic stat;
        for (int i = 0; i < NumTests; i++) {
          lomp::tsc_tick_count start;
          zeroToOther.release();
          uint64_t Tother = otherToZero.recv();
          lomp::tsc_tick_count end;
          // Assume that the communication in each direction takes the
          // same amount of time, then the offset is like this
          //
          //     Tstart
          //              Tcomms
          //                         Tother
          //              Tcomms
          //     Tend
          //
          // So in its own time,
          // TOtherStart = Tother-(Tend-Tstart)/2
          // then the offset to add to times in the other thread
          // to map them back to times in the initial thread
          // is Tstart - TOtherStart.
          // (Consider something like this
          // Tstart = 20
          //                  TOther=30
          // TEnd   = 30
          // Then we have
          // TComms = 5
          // TOthersStart = 30-5 = 25
          // Offset = 20-25 = -5, which is correct; placing Tother at 25,
          // half way between Tstart and Tend)
          // Ignore the first iteration
          if (i == 0)
            continue;
          int64_t Tstart = start.getValue();
          int64_t Tend = end.getValue();
          double Tcomms = (Tend - Tstart) / 2.0;
          double TOtherStart = Tother - Tcomms;
          double offset = Tstart - TOtherStart;
          stat.addSample(offset);
        }
        offsets[other] = int64_t(stat.getMean());
      }
      else if (me == other) {
        for (int i = 0; i < NumTests; i++) {
          zeroToOther.wait();
          otherToZero.send(lomp::tsc_tick_count::now().getValue());
        }
      }
#pragma omp barrier
    }
  }
}

enum { MAX_THREADS = 256, NUM_REPEATS = 20000 };

static void timeFullBarrier(int numThreads,
                            lomp::Barrier::barrierFactory factory,
                            lomp::statistic * LILO, lomp::statistic * LIMO,
                            int64_t const * offsets) {
  auto B = factory(numThreads);
  // This has false sharing, but it's not in the timed code.
  uint64_t entryTime[MAX_THREADS];
  uint64_t exitTime[MAX_THREADS];

#pragma omp parallel num_threads(numThreads)
  {}

#pragma omp parallel num_threads(numThreads)
  {
    auto me = omp_get_thread_num();
    lomp::randomDelay delayer(1023); // 1023*100ns = ~100us
    auto myEntry = &entryTime[me];
    auto myExit = &exitTime[me];
    auto myOffset = offsets[me];

    for (int i = 0; i < NUM_REPEATS; i++) {
      // Jitter which thread arrives last
      delayer.sleep();
      // Measure the times
      auto myEntryTime = lomp::tsc_tick_count::now();
      B->fullBarrier(me);
      auto myExitTime = lomp::tsc_tick_count::now();

      // Store them
      *myEntry = myEntryTime.getValue() + myOffset;
      *myExit = myExitTime.getValue() + myOffset;

      // Ensure that all of the exit times have been filled in!
      B->fullBarrier(me);

      // Now compute the statistics in thread zero.
      // Could use #pragma omp master, but this is fine too.
      if (me == 0) {
        uint64_t li = entryTime[0];
        uint64_t lo = exitTime[0];
        uint64_t sumO = 0;
        for (int i = 1; i < numThreads; i++) {
          li = std::max(li, entryTime[i]);
          lo = std::max(lo, exitTime[i]);
          sumO += exitTime[i];
        }
        LILO->addSample(lo - li);
        LIMO->addSample((sumO / numThreads) - li);
      }
      // Barrier again so that there's no bias towards thread zero arriving last
      // because it was doing the computation
      B->fullBarrier(me);
    }
  }
  delete B;
  fprintf(stderr, ".");
}

static void timeHalfBarrier(int numThreads,
                            lomp::Barrier::barrierFactory factory,
                            lomp::statistic * LIRO, lomp::statistic * RILO,
                            int64_t const * offsets) {
  auto B = factory(numThreads);

  if (B->isDistributed()) {
    lomp::errPrintf("%s is a distributed barrier, so we cannot measure LIRO "
                    "and RILO times\n");
    return;
  }
  // This has false sharing, but it's not in the timed code.
  uint64_t entryTime[MAX_THREADS];
  uint64_t exitTime[MAX_THREADS];

#pragma omp parallel
  {
    auto me = omp_get_thread_num();
    // Only use the first numThreads threads. This does mean others are waiting at an OpenMP
    // barrier while we're measuring, but that's probably not too big an issue.
    if (me < numThreads) {
      lomp::randomDelay delayer(1023); // 1023*100ns = ~100us
      auto myEntry = &entryTime[me];
      auto myExit = &exitTime[me];
      auto myOffset = offsets[me];
      bool root;
      uint64_t rootTime = 0;

      for (int i = 0; i < NUM_REPEATS; i++) {
        // Jitter which thread arrives last
        delayer.sleep();
        // Measure the times
        auto myEntryTime = lomp::tsc_tick_count::now();
        root = B->checkIn(me, true);
        auto CITime = lomp::tsc_tick_count::now();
        B->checkOut(root, me);
        auto myExitTime = lomp::tsc_tick_count::now();

        // Store them, adjusting for clock skew.
        *myEntry = myEntryTime.getValue() + myOffset;
        *myExit = myExitTime.getValue() + myOffset;
        if (root) {
          rootTime = CITime.getValue() + myOffset;
        }
        // Ensure that all of the exit times have been filled in!
        B->fullBarrier(me);

        // Now compute the statistics in whichever thread happened
        // to be the root.
        if (root) {
          uint64_t li = entryTime[0];
          uint64_t lo = exitTime[0];
          for (int i = 1; i < numThreads; i++) {
            li = std::max(li, entryTime[i]);
            lo = std::max(lo, exitTime[i]);
          }
          // Check sanity of the results before entering them.
          if (li < rootTime) {
            auto liroTime = rootTime - li;
            LIRO->addSample(liroTime);
          }
          if (lo > rootTime) {
            auto riloTime = lo - rootTime;
            RILO->addSample(riloTime);
          }
        }
        // Barrier again so that there's no bias towards whichever thread was root
        // arriving last because it was doing the stats computation; we have the
        // jitter, but this should help too.
        B->fullBarrier(me);
      }
    }
  }
  delete B;
  fprintf(stderr, ".");
}

// Not really a benchmark, rather a sanity test to try see if the barrier *is* a barrier
static void checkBarrier(int numThreads,
                         lomp::Barrier::barrierFactory factory) {
  auto B = factory(numThreads);
  int sequence[MAX_THREADS];

  for (int i = 0; i < MAX_THREADS; i++) {
    sequence[i] = 0;
  }
#pragma omp parallel
  {
    int me = omp_get_thread_num();
    lomp::randomDelay myJitter(2047);

    for (int i = 0; i < 1000; i++) {
      if (me < numThreads) {
        sequence[me] = i;
        // Delay each thread; could randonly delay all threads if we think
        // specific ordering or a more subtle race is a problem.
        if ((me % numThreads) == 0) {
          myJitter.sleep();
        }
        B->fullBarrier(me);
        for (int j = 0; j < numThreads; j++) {
          if (sequence[j] != i) {
            lomp::fatalError("%d in phase %d sees %d in phase %d\n", me, i, j,
                             sequence[j]);
          }
        }
        B->fullBarrier(me);
      }
    }
  }
  lomp::errPrintf("Barrier %s passed test with %d threads\n", B->name(),
                  numThreads);
  delete B;
}

static void testBarrier(lomp::Barrier::barrierDescription * desc) {
  lomp::errPrintf("Testing %s: %s barrier\n", desc->name, desc->getFullName());
  for (int i = 2; i <= omp_get_max_threads(); i++) {
    checkBarrier(i, desc->factory);
  }
}
static void printStat(lomp::statistic * stat, int nThreads) {
  // Convert to times
  double tickInterval = lomp::tsc_tick_count::getTickTime();
  stat->scale(tickInterval);

  printf("%7d, %s\n", nThreads, stat->format('s').c_str());
}

typedef void (*doTiming)(int, lomp::Barrier::barrierFactory, lomp::statistic *,
                         lomp::statistic *, int64_t const *);

enum measurementTag { LILO = 0, LIMO, LIRO, RILO, TEST, UNKNOWN };
char const * tags[] = {"LILO", "LIMO", "LIRO", "RILO", "TEST"};
static measurementTag findTag(char const * name) {
  auto av2 = std::string(name);

  for (auto tag = LILO; tag < UNKNOWN; tag = measurementTag(int(tag) + 1)) {
    if (av2 == tags[tag]) {
      return tag;
    }
  }
  return UNKNOWN;
}

static void runStats(doTiming measurementFn,
                     lomp::Barrier::barrierFactory factory,
                     measurementTag tag) {
  auto maxThreads = omp_get_max_threads();
  int64_t offsets[MAX_THREADS];
  auto statIdx = int(tag) & 1;
  computeClockOffset(&offsets[0]);

  // Run 1,2,4 threads, then by four up to the end
  lomp::statistic Stats[2];

  measurementFn(1, factory, &Stats[0], &Stats[1], &offsets[0]);
  printStat(&Stats[statIdx], 1);
  if (maxThreads < 2) {
    return;
  }
  Stats[0].reset();
  Stats[1].reset();
  measurementFn(2, factory, &Stats[0], &Stats[1], &offsets[0]);
  printStat(&Stats[statIdx], 2);
  if (maxThreads < 4) {
    return;
  }
  for (int i = 4; i <= maxThreads; i += 4) {
    Stats[0].reset();
    Stats[1].reset();
    measurementFn(i, factory, &Stats[0], &Stats[1], &offsets[0]);
    printStat(&Stats[statIdx], i);
  }
  if (maxThreads % 4 != 0) {
    Stats[0].reset();
    Stats[1].reset();
    measurementFn(maxThreads, factory, &Stats[0], &Stats[1], &offsets[0]);
    printStat(&Stats[statIdx], maxThreads);
  }
}

static void printHelp() {
  lomp::errPrintf(
      "The microbenchmark takes two or three arguments:\n"
      "Argument one is a measurement (LILO,LIMO,LIRO,RILO) or 'Test'\n"
      "Argument two is a barriername or All (ony with Test)\n"
      "Argument three is a number, to run the benchmark only at a specific "
      "number of threads.\n");
  lomp::Barrier::printBarriers();
  lomp::errPrintf("or 'omp'\n");
}

namespace lomp {
class ompBarrier : public Barrier {
public:
  ompBarrier(int) {}
  ~ompBarrier(){};
  static Barrier * newBarrier(int NumThreads) {
    return new ompBarrier(NumThreads);
  }
  void fullBarrier(int) {
#pragma omp barrier
  }
  bool checkIn(int) {
    fatalError("Cannot use checkIn in an onp, non-centralized, barrier\n");
  }

  void wakeUp(int, InvocationInfo const *) {
    fatalError("Cannot use wakeup in an omp, non-centralized, barrier\n");
  }

  InvocationInfo const * checkOut(bool, int) {
    fatalError("Cannot use wakeup in an omp, non-centralized, barrier\n");
  }
  static char const * barrierName() {
    return "OpenMP";
  }
  char const * name() const {
    return barrierName();
  }
};

} // namespace lomp

static lomp::Barrier::barrierDescription ompBarrierDesc = {
    "omp", lomp::ompBarrier::newBarrier, lomp::ompBarrier::barrierName};

int main(int argc, char ** argv) {
  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

  if (argc < 3) {
    lomp::errPrintf("Need an argument to choose the measurement, and one to "
                    "the barrier.\n");
    printHelp();
    return 1;
  }
  auto tag = findTag(argv[1]);
  if (tag == UNKNOWN) {
    lomp::errPrintf("Cannot find measurement %s\n", argv[1]);
    printHelp();
    return 1;
  }

  auto av2 = std::string(argv[2]);
  auto desc = lomp::Barrier::findBarrier(av2);
  if (desc == 0 && av2 == "omp") {
    // Use an openMP barrier
    desc = &ompBarrierDesc;
  }

  if (av2 == "All" && tag != TEST) {
    lomp::errPrintf("All can only be used with Test");
    printHelp();
    return 1;
  }
  if (desc == 0 && tag != TEST) {
    lomp::errPrintf("Cannot find barrier %s\n", argv[2]);
    printHelp();
    return 1;
  }

  if (argc == 4) {
    if ('0' <= argv[3][0] && argv[3][0] <= '9') {
      // A request to run with a specific number of threads.
      int64_t offsets[MAX_THREADS];
      computeClockOffset(&offsets[0]);

      lomp::statistic LILO;
      lomp::statistic LIMO;
      auto nThreads = atoi(argv[3]);
      timeFullBarrier(nThreads, desc->factory, &LILO, &LIMO, &offsets[0]);
      printf("Barrier %s Time\n"
             "%s, %s\n",
             "LILO", targetName.c_str(), desc->getFullName());
      printf(
          "Threads,    Count,        Min,      Mean,       Max,        SD\n");
      printStat(&LILO, nThreads);
      return 0;
    }
  }
  if (tag == TEST) {
    if (desc == 0) { // i.e. All
      for (int i = 0;; i++) {
        desc = lomp::Barrier::getBarrier(i);
        if (!desc) {
          break;
        }
        testBarrier(desc);
      }
    }
    else {
      testBarrier(desc);
    }
    return 0;
  }
  // Split the barrier name into two components at the semicolon
  // if we're measuring LIRO or RILO, so that the checkin and checkout show up as
  // separate parameters to our plotting code, and therefore get considered
  // as an axis to be shown in a uniform way.
  std::string barrierName = desc->getFullName();
  if (tag == LIRO || tag == RILO) {
    auto pos = barrierName.find(";");
    if (pos != std::string::npos) {
      barrierName[pos] = ',';
    }
  }
  printf("Barrier %s Time\n"
         "%s, %s\n",
         tags[tag], targetName.c_str(), barrierName.c_str());
  printf("Threads,    Count,        Min,      Mean,       Max,        SD\n");
  auto measurementFunction =
      (tag == LIRO || tag == RILO) ? timeHalfBarrier : timeFullBarrier;

  runStats(measurementFunction, desc->factory, tag);

  return 0;
}
