//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

#if (0)
/* This does not work since the relevant registers do not seem to be accessible
 * from unprivileged code. :-(
 */
uint32_t getCacheLineWidth(int Level) {
  uint64_t Res;
  __asm__ volatile("msr \tccselr_el1,%1\n\t"
                   "mrs \t%0, ccsidr_el1"
                   : "=r"(Res)
                   : "r"(Level << 1));
  return 1 << ((Res & 7) + 4);
}
#endif

#define GENERATE_READ_SYSTEM_REGISTER(ResultType, FuncName, Reg)               \
  ResultType funcName() {                                                      \
    uint64_t Res;                                                              \
    __asm__ volatile("mrs \t%0," #Reg : "=r"(Res));                            \
    return Res;                                                                \
  }

GENERATE_READ_SYSTEM_REGISTER(uint64_t, GetHRTime, cntvct_el0)
GENERATE_READ_SYSTEM_REGISTER(uint32_t, GetHRFreq, cntfrq_el0)

void flushCache(void * addr) {
  __asm__ volatile("dc civac,%0" ::"r"(addr));
}
