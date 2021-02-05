//===-- cvwake.cc - Measure time to wake a thread ---------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a micro-benchmark to measure the time to wake a thread.
///
#include <thread>
#include <unistd.h>
#include <atomic>
#include <linux/futex.h>
#include <sys/syscall.h>
#include "stats-timing.h"

static void delay(uint32_t usec) {
  uint32_t ticks = usec * 1.e-6 / lomp::tsc_tick_count::getTickTime();
  lomp::tsc_tick_count end =
      lomp::tsc_tick_count(lomp::tsc_tick_count::now().getValue() + ticks);

  while (lomp::tsc_tick_count::now().before(end))
    ;
}

int futex(int * uaddr, int futex_op, int val, const struct timespec * timeout,
          int * uaddr2, int val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

void futex_wait(int * futex_addr, int val) {
  int ret = futex(futex_addr, FUTEX_WAIT, val, NULL, NULL, 0);
  (void)ret;
}

void futex_wake(int * futex_addr, int nwait) {
  int futex_rc = futex(futex_addr, FUTEX_WAKE, nwait, NULL, NULL, 0);
  if (futex_rc == -1) {
    perror("futex wake");
    exit(1);
  }
}

int futexAddr = 0;
std::atomic<bool> status(false);
std::atomic<bool> done(false);
int threadWakeups = 0;

void worker_thread() {
  while (!done) {
    while (!status)
      ;
    futex_wait(&futexAddr, 1);
    status = false;
    threadWakeups += 1;
  }
}

enum { NUM_SAMPLES = 1000 };

int main(int, char **) {
  lomp::statistic stat;
  std::thread worker(worker_thread);

  for (int i = 0; i < NUM_SAMPLES; i++) {
    status = true;
    delay(10000);
    {
      TIME_BLOCK(&stat);
      futex_wake(&futexAddr, 1);
      while (status)
        ;
    }
  }
  done = true;
  status = true;
  futex_wake(&futexAddr, 1);

  stat.scale(lomp::tsc_tick_count::getTickTime());
  printf("CV Wakeup time\n"
         "Futex\n"
         "Samples,       Min,      Mean,       Max,        SD\n"
         "%s\n",
         stat.format('s').c_str());

  printf("Thread wakeups %d\n", threadWakeups);
  worker.join();
  return 0;
}
