//===-- checkRandom.cc - Check our mlfsr ------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a simple test for the mlfsr random number generator

#include "mlfsr32.h"
#include "stats-timing.h"

// Just check 16 bits worth, but we can choose where we take them from
// This is *not* a serious check for randomness, which is hard, (Knuth
// has a lot on this if you want deep background, though there is more
// up to date research, of course!).
// We already know that this is not a good random number generator.
// Rather this just checks that we do see some semi-sane distribution of
// numbers, so our code isn't stuck in the "all zeroes" state, or has
// another gross problem!
//
// Validation here is by eye :-)
//
void check(lomp::mlfsr32 * generator, uint32_t shift) {
  lomp::statistic stat(true);
  int counts[1 << 16] = {0};

  for (int i = 0; i < (1 << 16); i++) {
    uint32_t value = (generator->getNext() >> shift) & 0xffff;
    counts[value]++;
  }
  for (int i = 0; i < (1 << 16); i++)
    stat.addSample(counts[i]);

  printf("Shift: %d\n", shift);
  printf(" Samples ,    Min   ,    Mean  ,    Max   ,     SD\n%s\n",
         stat.format(' ').c_str());
  printf("%s\n", stat.formatHist(' ').c_str());
}

int main(int, char **) {
  lomp::mlfsr32 generator;

  for (int shift = 0; shift <= 16; shift++)
    check(&generator, shift);

  return 0;
}
