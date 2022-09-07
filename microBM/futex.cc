//===-- futex.cc - Measure time to wake a thread ----------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a micro-benchmark to measure the time to wake a thread
/// via a Linux futex; it only makes sense on Linux...
///
#include <thread>
#include <unistd.h>
#include <atomic>
#include <climits>
#include <ctime>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <omp.h>
#include "stats-timing.h"
#include "channel.h"

// General acces to the futex call
static int32_t futex(int32_t * uaddr, int32_t futex_op, int32_t val,
                     const struct timespec * timeout, int32_t * uaddr2,
                     int32_t val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

// Wait
static void futexWait(int32_t * futexAddr, int32_t currentVal) {
  do {
    (void)futex(futexAddr, FUTEX_WAIT, currentVal, NULL, NULL, 0);
  } while (*futexAddr == currentVal);
}

// Wake
void futexWakeAll(int32_t * futex_addr) {
  int32_t futex_rc = futex(futex_addr, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
  if (futex_rc == -1) {
    perror("futex wake");
    exit(1);
  }
}

template <int32_t Ready, int32_t NotReady>
class futexSleep {
  // Put the counter and the flag in separate cache lines.
  // Most of the time no-one need touch "sleeping", we hope.
  CACHE_ALIGNED std::atomic<int32_t> sleeping;

protected:
  CACHE_ALIGNED std::atomic<int32_t> go;

public:
  futexSleep() {
    reset();
  }

  ~futexSleep() {}

  void reset() {
    go.store(NotReady, std::memory_order_release);
    sleeping.store(0, std::memory_order_release);
  }

  // Not normally needed, but useful for the micro-benchmark
  int32_t sleepCount() const {
    return sleeping;
  }

  void wait() {
    // Now suspend on a futex while ensuring that the thread which
    // will wake us knows that.
    // Set the flag to say that someone is sleeping, then call the futex wait which atomically
    // re-checks that go is still NotReady before sleeping.
    sleeping++;
    // Call the futex_wait.
    futexWait((int32_t *)&go, NotReady);
  }

  // We separate the soft and hard kicks so that if a waking thread is trying to wake
  // many different beds it can give them all a soft kick before it then comes back
  // and kicks them all hard. That way the latency for the whole operation may be better
  // when no-one is sleeping, since the other threads can start before the root has
  // had to make any system calls.
  // Set the flag, but don't even test whether the futex is needed yet.
  void kickSoftly() {
    go.store(Ready, std::memory_order_release);
  }

  // Just perform the futex operation if required.
  void kickHard() {
    // Now check whether we also need to make the futex call.
    if (sleeping.load(std::memory_order_acquire) != 0) {
      futexWakeAll((int32_t *)&go);
    }
  }

  // Do both operations
  void kick() {
    kickSoftly();
    kickHard();
  }
};

#if (0)
#error not working yet...
// Code to poll but then back off to a futex wait.
// The complexity here is in ensuring that
// 1) all threads which sleep do get woken, and there isn't a race in which the root thread fails to wake anyone
// 2) the root thread does not make a system call unless it needs to.
//
// This code is intended for the broadcast operation in a barrier,
// not to wakeup threads which are waiting to claim a lock.
// For that it would be better to use the std::condition_variable operations.
// Those are not helpful here, though, bnecause they require that threads acquire
// a lock, which is not what we want (or need) here.

// You sleep in a bed :-)
template <int32_t Ready, int32_t NotReady>
class bed : private futexSleep<Ready, NotReady> {
public:
  bed() {}
  ~bed() {}

  void reset() {
    futexSleep<Ready, NotReady>::reset();
  }

  void wait() {
    // Try once before doing anything else
    if (go.load(std::memory_order_acquire) == Ready) {
      return;
    }
    // Try another five times (arbitrary and unlikely to be the right choice)
    for (int32_t i = 0; i < 5; i++) {
      if (go.load(std::memory_order_acquire) == Ready) {
        return;
      }
    }
    // Exponential backoff with yield
    randomExponentialBackoff backoff;
    do {
      backoff.sleep();
      if (go.load(std::memory_order_acquire) == Ready) {
        return;
      }
    } while (!backoff.atLimit());

    futexSleep::wait();
  }
};
#endif

#define NumSamples 10000

static void futexRootTime(lomp::statistic * stat, int const numWaiters) {
  futexSleep<1, 0> f;

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    for (int i = 0; i < NumSamples; i++) {
      if (me == 0) {
        while (f.sleepCount() != numWaiters) {
        }
        f.kickSoftly();
        TIME_BLOCK(stat);
        f.kickHard();
      }
      else if (me <= numWaiters) {
        f.wait();
      }
#pragma omp barrier
      if (me == 0) {
        f.reset();
      }
#pragma omp barrier
    }
  }
}

static void measureRootTime(lomp::statistic * stats, int maxNumWaiters) {
  for (int i = 0; i <= maxNumWaiters; i++) {
    futexRootTime(&stats[i], i);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}

static void futexRILOTime(lomp::statistic * stat, int const numWaiters,
                          int64_t const * offsets) {
  // This has false sharing, but it's not in the timed code.
  uint64_t exitTime[LOMP_MAX_THREADS];
  futexSleep<1, 0> f;

#pragma omp parallel
  {
    auto me = omp_get_thread_num();
    auto myExit = &exitTime[me];
    auto myOffset = offsets[me];
    lomp::tsc_tick_count rootTime;

    for (int i = 0; i < NumSamples; i++) {
      if (me == 0) {
        while (f.sleepCount() != numWaiters) {
        }
        f.kickSoftly();
        rootTime = lomp::tsc_tick_count::now();
        f.kickHard();
      }
      else if (me <= numWaiters) {
        f.wait();
        *myExit = lomp::tsc_tick_count::now().getValue() + myOffset;
      }
#pragma omp barrier
      if (me == 0) {
        f.reset();
        uint64_t lo = exitTime[1];
        for (int i = 0; i < numWaiters - 1; i++) {
          lo = std::max(lo, exitTime[i + 2]);
        }
        stat->addSample(lo - rootTime.getValue());
      }
#pragma omp barrier
    }
  }
}

static void measureRILOTime(lomp::statistic * stats, int maxNumWaiters,
                            int64_t * offsets) {
  for (int i = 0; i <= maxNumWaiters; i++) {
    futexRILOTime(&stats[i], i, offsets);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}

static void printHelp() {
  printf("L            -- Last out time\n");
  printf("M            -- Root time\n");
}

static void printStats(lomp::statistic * stats, int count, int offset = 1) {
  // Convert to times
  double tickInterval = lomp::tsc_tick_count::getTickTime();
  for (int i = 0; i < count; i++)
    stats[i].scale(tickInterval);

  for (int i = 0; i < count; i++)
    printf("%6d,         %s\n", i + offset, stats[i].format('s').c_str());
}

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

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

int main(int argc, char ** argv) {
  // Check that alignment is working
  int nThreads = omp_get_max_threads();
  if (nThreads > LOMP_MAX_THREADS) {
    printf("%d threads available, increase LOMP_MAX_THREADS (%d)\n", nThreads,
           LOMP_MAX_THREADS);
    return 1;
  }

  if (nThreads < 2) {
    printf("Need more than one thread\n");
    return 1;
  }

  if (argc != 2) {
    printf("Need an argument\n");
    printHelp();
    return 1;
  }

  // Read relevant envirables and remember the info
  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

    // Warm up...
#pragma omp parallel
  { forceAffinity(); }
  int64_t offsets[LOMP_MAX_THREADS];
  computeClockOffset(&offsets[0]);

  lomp::statistic threadStats[LOMP_MAX_THREADS];
  lomp::statistic * stats = &threadStats[0];
  // Most of the tests are per-thread but don't measure zero to zero.
  int numStats = nThreads;
  int base = 0;

  switch (argv[1][0]) {
  case 'L':
    measureRILOTime(stats, nThreads - 1, &offsets[0]);
    printf("futex RILO time\n"
           "%s\n"
           "# %s\n"
           "Waiting Threads,  Samples,       Min,      Mean,       Max,        "
           "SD\n",
           targetName.c_str(), getDateTime().c_str());
    base = 1;
    numStats = numStats - 1;
    break;
  case 'R':
    measureRootTime(stats, nThreads - 1);
    printf("futex RIRO time\n"
           "%s\n"
           "# %s\n"
           "Waiting Threads,  Samples,       Min,      Mean,       Max,        "
           "SD\n",
           targetName.c_str(), getDateTime().c_str());
    break;
  default:
    printf("Unknown experiment\n");
    printHelp();
    return 1;
  }
  printStats(&stats[base], numStats, base);
  return 0;
}
