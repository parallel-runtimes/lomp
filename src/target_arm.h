//===-- target_arm.h - Target architecture definitions (ARM) ----*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_ARM_H_INCLUDED
#define TARGET_ARM_H_INCLUDED

#ifndef TARGET_H_INCLUDED
#error "Thou shalt not include this file outside target.h!"
#endif

// Properties of the ABI: how many register arguments are there?
#define MAX_REGISTER_ARGS 8

// What is the cacheline size?
// https://en.wikichip.org/wiki/cavium/microarchitectures/vulcan#Memory_Hierarchy
// says "Vulcan's L2 cache is 256 KiB, half that of prior design, and
// has an L2 to L1 bandwidth of 64 bytes per cycle in either
// direction."
// So 64B seems correct, though not on the Apple M1, where
// sysctl tells us hw.cachelinesize: 128
// We therefore assume that if we're an Arm on MacOS we're on an
// Apple machine with 128B cache line size. (Dubious, but effective for now).
// Similarly the page size is 4KiB on Linux, but 16KiB on MacOS/M1
#if (LOMP_TARGET_MACOS)
// Values from sysctl
// hw.cachelinesize: 128
// hw.pagesize: 16384
#define CACHELINE_SIZE 128
#define PAGE_SIZE (16 * 1024)
#else

#define CACHELINE_SIZE 64
// getconf PAGE_SIZE returns 4096, on Linux/Arm so this also seems correct.
#define PAGE_SIZE (4 * 1024)
#endif

#if (LOMP_TARGET_ARCH_AARCH64)
#if (1)
// Disable here to test the std::chrono clock if desired.
#define TARGET_HAS_CYCLECOUNT 1
#define TARGET_HAS_HWTICKTIME 0
#define TARGET_HAS_CACHE_FLUSH 1

// Setup functions we need for accessing the high resolution clock
#define GENERATE_READ_SYSTEM_REGISTER(ResultType, FuncName, Reg)               \
  inline ResultType FuncName() {                                               \
    uint64_t Res;                                                              \
    __asm__ volatile("mrs \t%0," #Reg : "=r"(Res));                            \
    return Res;                                                                \
  }
GENERATE_READ_SYSTEM_REGISTER(uint64_t, readCycleCount, cntvct_el0)
GENERATE_READ_SYSTEM_REGISTER(uint32_t, getHRFreq, cntfrq_el0)
GENERATE_READ_SYSTEM_REGISTER(uint64_t, getArmID, midr_el1)
// Tidiness for those including this
#undef GENERATE_READ_SYSTEM_REGISTER

inline void FlushAddress(void * addr) {
  __asm__ volatile("dc civac,%0" ::"r"(addr));
}
#else
#define TARGET_HAS_CYCLECOUNT 0
#define TARGET_HAS_HWTICKTIME 0
#define TARGET_HAS_CACHE_FLUSH 0
#endif

#elif (LOMP_TARGET_ARCH_ARMV7L)
#define TARGET_HAS_CYCLECOUNT 0
#define TARGET_HAS_HWTICKTIME 0
#define TARGET_HAS_CACHE_FLUSH 0
#else
#error Unknown ARM architedture.
#endif

/* Arm has SMT by default so we expose the yield instruction unless explicitly told not to */
#if (!defined(USE_YIELD))
#define USE_YIELD 1
#endif

#if (USE_YIELD)
// The hint instruction to the core that this thread is spinning
inline void Yield() {
  __asm__("yield");
}
#endif

// Speculative execution instructions. We don't have any Arm processors
// with these in yet, so we just say that and use the defaults which do nothing.
#define TARGET_HAS_SPECULATION 0
#endif
