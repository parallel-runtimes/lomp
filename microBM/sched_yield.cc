//===-- sched_yield.cc - Measure sched_yield behaviour -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///This file contains micro-benchmarks to elaborate the
/// behaviour of Linux' sched_yield system call.
///

// Needed to be able to use the per-thread aspects of getrusage
#if (!defined(_GNU_SOURCE))
# define _GNU_SOURCE 1
#endif

#include <cstdint>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <thread>

#include "stats-timing.h"
#include "target.h"
#include <omp.h>

#if (LOMP_TARGET_LINUX)
// Get sched_yield()
#include <sched.h>
#include <sys/resource.h>

using namespace lomp;

// Get the elapsed CPU time for a thread in microseconds.
static uint64_t getThreadCPUTime() {
  struct rusage usage;
  
  if (getrusage(RUSAGE_THREAD, &usage) != 0) {
    fatalError("getrusage failed: %d.\n", errno);
    exit(-1);
  }

  uint64_t uSec = usage.ru_utime.tv_sec*1000000+usage.ru_utime.tv_usec +
    usage.ru_stime.tv_sec*1000000+usage.ru_stime.tv_usec;
  // fprintf(stderr, "***Rusage CPU-time %lu uS***\n", uSec);
  return uSec;
}

class CPUBlockTimer {
  uint64_t Start;
  statistic * Stat;

public:
  CPUBlockTimer(statistic * s) : Stat(s) {
    Start = getThreadCPUTime();
  }
  ~CPUBlockTimer() {
    Stat->addSample(getThreadCPUTime() - Start);
  }
};

#define CPUTIME_BLOCK(s) CPUBlockTimer __bt__((s))

static void reference(uint32_t value) {
  __asm__ volatile("# Ensure compiler doesn't eliminate the value"
                   :
                   : "r"(value));
}
// Force thread affinity.
// This is a dubious thing to do; it'd be better to use the functionality
// of the OpenMP runtime, but this is simple, at least.
#include <sched.h>
void forceAffinity(int logicalCPU) {
  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(logicalCPU, &set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0)
    fprintf(stderr, "Failed to force affinity for thread %d to logicalCPU %d\n", omp_get_thread_num(), logicalCPU);
}

#include <complex>
// Some work which is not completely trivial.
// Count the number of cells in the Mandelbrot set for a suitably
// sized array mapped over -2..2, -2..2 This is not the way one would
// sanely do this for performance (there are unnecessary square root
// operations here!), but it doesn't matter for our purposes.
// 
static uint32_t countMandelCells(int gridSize) {
  uint32_t cellsOut = 0;
  for (auto i=0; i<gridSize; i++) {
    auto xpos = double(i/4.0) - 2.0;
    for (auto j=0; j<gridSize; j++) {
      auto ypos = double(j/4.0) - 2.0;
      std::complex<double> position(xpos,ypos);
      std::complex<double> current(0.0,0.0);
      for (auto count=0; count < 500; count++) {
        // Mandelbrot computation
        current = current*current + position;
        if (abs(current) >= 2.0) {
          cellsOut++;
          break;
        }
      }
    }
  }
  return gridSize*gridSize-cellsOut;
}

static void interfere(std::atomic<bool> * flag, statistic *stat){
  TIME_BLOCK(stat);
  while (!*flag)
    ;
}

static void interfereWithSchedYield(std::atomic<bool> * flag, statistic *stat){
  while (!*flag)
    {
      TIME_BLOCK(stat);
      sched_yield();
    }
}

static void interfereWithStdYield(std::atomic<bool> * flag, statistic * stat){
  while (!*flag)
    {
      TIME_BLOCK(stat);
      std::this_thread::yield();
    }
}

typedef void (*interferenceFunction)(std::atomic<bool> *, statistic *);

struct CPUtimes {
  double work;
  double poll;
  CPUtimes(): work(0), poll(0) {}
  CPUtimes(uint64_t w, uint64_t p) : work(double(w)*1.e-6), poll(double(p)*1.e-6) {}
};

#define PROBLEM_SIZE 1000
static CPUtimes timeWork(statistic * pollStat, statistic * workStat,
			 interferenceFunction poller) {
  std::atomic<bool> done = false;
  uint64_t pollCPU;
  uint64_t workCPU;
  
  // Initialise threads
#pragma omp parallel num_threads(2)
  {
    if (omp_get_thread_num() == 0) {
      auto startCPU = getThreadCPUTime();
      poller (&done, pollStat);
      pollCPU = getThreadCPUTime()-startCPU;
    } else {
      auto startCPU = getThreadCPUTime();
      uint32_t total = 0;
      for (int i=0; i<100; i++) {
        TIME_BLOCK(workStat);
	total += countMandelCells(PROBLEM_SIZE);
      }
      reference(total);
      done = true;
      workCPU = getThreadCPUTime()-startCPU;
    }
  }
  return CPUtimes(workCPU, pollCPU);
}

static CPUtimes timeSerial(statistic *stat) {
  uint32_t total = 0;
  auto startCPU = getThreadCPUTime();
  for (int i=0; i<100; i++) {
    TIME_BLOCK(stat);
    
    total += countMandelCells(PROBLEM_SIZE);
  }
  reference(total);
  return CPUtimes(getThreadCPUTime()-startCPU, 0);
}

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

int main(int, char **) {
  std::string targetName = Target::CPUModelName();
  statistic pollStats[4]{statistic(false), statistic(false), statistic(false)};
  statistic workStats[4]{statistic(false), statistic(false), statistic(false)};
  static char const * caseNames[] = {"Serial", "No yield", "sched_yield", "std::this_thread::yield"};
  CPUtimes CPU[4];

  CPU[0]= timeSerial(&workStats[0]);
  fprintf(stderr,"Done serial\n");
  #pragma omp parallel num_threads(2)
  {
    // Force all threads onto the same logicalCPU
    forceAffinity(2);
  }
  CPU[1] = timeWork(&pollStats[1], &workStats[1], interfere);
  fprintf(stderr,"Done no yield\n");
  CPU[2] = timeWork(&pollStats[2], &workStats[2], interfereWithSchedYield);
  fprintf(stderr,"Done sched_yield\n");
  CPU[3] = timeWork(&pollStats[3], &workStats[3], interfereWithStdYield);
  fprintf(stderr,"Done std::this_thread::yield\n");

  double tickInterval = lomp::tsc_tick_count::getTickTime();
  
  for (auto i = 0; i<4; i++) {
    statistic * workStat = &workStats[i];
    workStat->scale(tickInterval);
    printf("Work time\n"
	   "# %s, %s"
	   "%s\n"
	   "Count,       Min,      Mean,       Max,        Total,     SD\n",
	   targetName.c_str(), getDateTime().c_str(), caseNames[i]);
    printf("%s\n", workStat->format('s',true).c_str());
    printf("Work CPU: %s\n", lomp::formatSeconds(CPU[i].work,8).c_str());
    statistic * pollStat = &pollStats[i];
    pollStat->scale(tickInterval);
    printf("Poll time\n"
	   "# %s, %s"
	   "%s\n"
	   "Count,       Min,      Mean,       Max,        Total,     SD\n",
	   targetName.c_str(), getDateTime().c_str(), caseNames[i]);

    printf("%s\n", pollStat->format('s',true).c_str());
    printf("Polling CPU: %s\n", lomp::formatSeconds(CPU[i].poll,8).c_str());
    if (i != 3) {
      printf("\n### NEW EXPERIMENT ###\n");
    }
  }
  
  return 0;
}
#else
int main (int, char **) {
  fprintf (stderr,"This benchmark measures a Linux specific system call, so makes no sense elsewhere.\n");
  return 0;
}
#endif

