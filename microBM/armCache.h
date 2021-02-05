//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

uint32_t getCacheLineWidth(int Level) {
  uint64_t Res;
  __asm__ volatile("msr \tccselr_el1,%1\n\t"
                   "mrs \t%0, ccsidr_el1"
                   : "=r"(Res)
                   : "r"(Level << 1));
  return 1 << ((Res & 7) + 4);
}

uint64_t getHRCLock() {
  uint64_t Res;
  __asm__ volatile("mrs \t%0, cntvct_el0" : "=r"(Res) : :);
  return Res;
}
