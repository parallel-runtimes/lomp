//===-- mlfsr.h - Maximum Length Feedback Shift Register --------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the code for a maximum length feedback shift register random
/// number genereator.
/// It is fast and good enough for generating random delays in back-offs, but certainly
/// not for anything like cryptography or Monte Carlo computations.

#ifndef MLFSR_H
#define MLFSR_H
#include <cstdint>

// For the timers
#include "stats-timing.h"

namespace lomp {
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
    if (State & 1) {
      // Magic number from https://users.ece.cmu.edu/~koopman/lfsr/index.html
      State = (State >> 1) ^ 0x80000057;
    }
    else {
      State = State >> 1;
    }
    return State - 1; // So that we can return zero.
  }
};

// Provide a random exponential backoff.
// We use the CPU "cycle" count timer to provide
// delay between around 100ns and 25us.
// Each delay step is executed twice
class randomExponentialBackoff {
  // multiplier used to convert our units into timer ticks
  static uint32_t timeFactor;

  mlfsr32 random;
  uint32_t mask;          // Limits current delay
  enum { maxMask = 255 }; // 256*100ns = 25.6us
  uint32_t sleepCount;    // How many times has sleep been called
  uint32_t delayCount;    // Only needed for stats

  enum {
    initialMask = 1,
    delayMask = 1, // Do two delays at each exponential value
  };

public:
  randomExponentialBackoff() : mask(1), sleepCount(0), delayCount(0) {}
  void sleep() {
    uint32_t count = 1 + (random.getNext() & mask);
    delayCount += count;
    lomp::tsc_tick_count end =
        lomp::tsc_tick_count::now().getValue() + delayCount * timeFactor;
    // Up to next power of two if it's time to ramp.
    if ((++sleepCount & delayMask) == 0)
      mask = ((mask << 1) | 1) & maxMask;
    // And delay. If yield() takes > 100ns (as it may well on some Intel
    // processors), so be it.
    while (lomp::tsc_tick_count::now().before(end)) {
      Target::Yield();
    }
  }
  bool atLimit() const {
    return mask == maxMask;
  }
  uint32_t getDelayCount() const {
    return delayCount;
  }
};

class randomDelay {
  mlfsr32 random;
  static uint32_t
      timeFactor; // multiplier used to convert our units into timer ticks
  uint32_t mask;

public:
  randomDelay(uint32_t maxMask) : mask(maxMask) {}
  void sleep() {
    uint32_t count = random.getNext() & mask;
    lomp::tsc_tick_count end =
        lomp::tsc_tick_count::now().getValue() + count * timeFactor;
    while (lomp::tsc_tick_count::now().before(end)) {
      ;
    }
  }
};
} // namespace lomp
#endif
