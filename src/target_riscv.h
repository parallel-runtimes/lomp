//===-- target_riscv.h - Target architecture definitions (RISCV) *- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_RISCV_H_INCLUDED
#define TARGET_RISCV_H_INCLUDED

#ifndef TARGET_H_INCLUDED
#error "Thou shalt not include this file outside target.h!"
#endif

#define CACHELINE_SIZE 64
#define PAGE_SIZE 4096
#define MAX_REGISTER_ARGS 8

/* Supposedly the RDTIME instruction should be able to extract a constant time
 * monotonic timer.
 * 
 * See https://elixir.bootlin.com/linux/v5.10.8/source/arch/riscv/include/asm/timex.h for Linux
 * code for this, which simply reads the underlying CSR.
 * The relevant CSRs are supposed to be visible from user-space.
 *
 * Note that the clang built in (__builtin_readcyclecounter()) does what it says on the tin
 * and reads the cycle counter which is not constant frequency or cross thread synchronized.
 * What we want it the clock which is constant and synchronized. (i.e. CSR 0xC01 rather than 0xC00).
 * 
 * For 32b code things are slightly more complex, since the value is across two 32b registers,
 * so one has to be careful of a race in which there's a carry between the two in-between reading
 * them... (So you must read hi, read low, read hi and compare with previous value; code for
 * this is all there).
 */

#define TARGET_HAS_TIMESTAMP 0

#if (!defined(USE_YIELD))
#define USE_YIELD 0
#endif

#if (USE_YIELD)
inline void Yield() { /* do nothing for now */
#if (LOMP_WARN_ARCH_FEATURES)
#warning "The Yield() function is a no-op for RISC-V!"
#endif
}
#endif

#define TARGET_HAS_CACHE_FLUSH 0

// Speculative execution instructions. RISC-V does not (yet) have this feature,
// so we use the default no-op implementation from target.h. (Though there is a
// placeholder for support for this in the architecture manual!)
#define TARGET_HAS_SPECULATION 0
#endif
