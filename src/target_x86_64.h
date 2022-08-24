//===-- target_x86_64.h - Target architecture definitions (x86) -*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_X86_64_H_INCLUDED
#define TARGET_X86_64_H_INCLUDED

#ifndef TARGET_H_INCLUDED
#error "Thou shalt not include this file outside target.h!"
#endif

#include <x86intrin.h>
#include <immintrin.h>

// Properties of the ABI, how many register arguments are there?
#define MAX_REGISTER_ARGS 6
#define CACHELINE_SIZE 64
#define PAGE_SIZE 4096

#define TARGET_HAS_CYCLECOUNT 1
#define TARGET_HAS_HWTICKTIME 1
#if (defined(USING_BUILTIN_CYCLECOUNT))
#undef USING_BUILTIN_CYCLECOUNT
#else
inline uint64_t readCycleCount() {
  return __rdtsc();
}
#endif

/* X86_64 has SMT by default so we expose the yield instruction unless explicitly told not to */
#if (!defined(USE_YIELD))
#define USE_YIELD 1
#endif

#if (USE_YIELD)
// The hint instruction to the core that this thread is spinning
inline void Yield() {
  _mm_pause();
}
#endif

#define TARGET_HAS_CACHE_FLUSH 1
inline void FlushAddress(void * addr) {
  __asm__ volatile("clflush (%0)" ::"r"(addr));
}

// For the moment don't enable speculation on MacOS.
// This is not correct; there are Apple machines with Intel processors
// that have the feature, but a quick hack... (And it isn't enabled
// in the X86_64 emulator on the M1, which is what I'm interested in at present :-)).
#define TARGET_HAS_SPECULATION (!LOMP_TARGET_MACOS)
#if (TARGET_HAS_SPECULATION)
// Handle speculative execution, here Intel Restricted Transactional Memory (RTM)
// Test whether the machine has RTM and it is enabled.
inline bool HaveSpeculation() {
  struct cpuid_t {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
  } cpuid;

  __asm__ __volatile__("cpuid"
                       : "=a"(cpuid.eax), "=b"(cpuid.ebx), "=c"(cpuid.ecx),
                         "=d"(cpuid.edx)
                       : "a"(7), "c"(0));
  return (cpuid.ebx & (1 << 11)) != 0;
}

inline int32_t StartSpeculation() {
  return _xbegin();
}
inline bool InSpeculation() {
  return _xtest();
}
inline void CommitSpeculation() {
  _xend();
}

// Tag must be a compile time known constant, since it's an immediate in the
// instruction.  So we seem forced to have to use a macro, even though after
// inlining the compiler should be able to see that there is a literal constant
// at the call site!
#if defined(__clang__)
#define Target_AbortSpeculation(tag) _xabort(tag)
#else
#define Target_AbortSpeculation(tag) Target::_xabort(tag)
#endif

#endif /* TARGET_HAS_SPECULATION */

#endif
