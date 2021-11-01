//===-- target.h - Target architecture definitions --------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

///
/// \file
/// This file contains interfaces to architecture dependent functions, and some
/// other generally useful macros.
///
//===----------------------------------------------------------------------===//
#ifndef TARGET_H_INCLUDED
#define TARGET_H_INCLUDED

#include <cstdlib>
#include <cstdint>

#include <string>
#include <chrono>

// Determine the OS support to not die in compilation-guard vein
#if (__APPLE__ && __MACH__)
#define LOMP_TARGET_MACOS 1
#elif (defined(LINUX) || defined(__linux__) || defined(__linux))
#define LOMP_TARGET_LINUX 1
#else
#error Unknown operating system
#endif

// Determine the target architecture
#if (__x86_64__)
#define LOMP_TARGET_ARCH_X86_64 1
#define LOMP_TARGET_ARCH_NAME "x86_64"
#elif (__aarch64__)
#define LOMP_TARGET_ARCH_AARCH64 1
#define LOMP_TARGET_ARCH_NAME "aarch64"
#elif (__arm__)
#define LOMP_TARGET_ARCH_ARMV7L 1
#define LOMP_TARGET_ARCH_NAME "armv7l"
#elif (__riscv)
#define LOMP_TARGET_ARCH_RISCV 1
#define LOMP_TARGET_ARCH_NAME "riscv"
#else
#error "Unknown target architecture"
#endif

// Check for the existence of 128-bit int
#if defined(__SIZEOF_INT128__)
#define LOMP_HAVE_INT128T 1
#endif

// Miscellaneous macros
#define LIKELY(cond) __builtin_expect(int(cond), 1)
#define UNLIKELY(cond) __builtin_expect(int(cond), 0)

// Expand a macro and convert the result into a string
#define STRINGIFY1(...) #__VA_ARGS__
#define STRINGIFY(...) STRINGIFY1(__VA_ARGS__)

namespace Target {
// Compiler dependent stuff
// Order matters here. LLVM also sets __GNUC__ ...

// Portability macros for pragmas or attributes

// C++17 features
#if (__cplusplus >= 201703L)
#define FALLTHROUGH [[fallthrough]]
#endif

#if defined(__cray__)
#define COMPILER_NAME "Cray: " __VERSION__
#elif defined(__INTEL_COMPILER)
#define COMPILER_NAME                                                          \
  "Intel: " STRINGIFY(__INTEL_COMPILER) "v" STRINGIFY(                         \
      __INTEL_COMPILER_BUILD_DATE)
#elif defined(__clang__)
#define COMPILER_NAME                                                          \
  "LLVM: " STRINGIFY(__clang_major__) ":" STRINGIFY(                           \
      __clang_minor__) ":" STRINGIFY(__clang_patchlevel__)
#define UNROLL_LOOP _Pragma("unroll")
#ifndef FALLTHROUGH
#define FALLTHROUGH [[clang::fallthrough]]
#endif
#elif defined(__GNUC__)
#define COMPILER_NAME "GCC: " __VERSION__
#define UNROLL_LOOP _Pragma("GCC unroll")
#ifndef FALLTHROUGH
#define FALLTHROUGH [[gnu::fallthrough]]
#endif
#else
#define COMPILER_NAME "Unknown compiler"
#endif

#if (!defined(UNROLL_LOOP))
#define UNROLL_LOOP
#endif
#if (!defined(FALLTHROUGH))
#define FALLTHROUGH
#endif

// This gives us a useless timer, at least with Cray 9.0.1 compiler, even though
// one would hope that it is the right way to do this!
// On RISC-V it would use the cycle-counter, rather than the timer, and on
// AArch64 the performance monitor counter, rather than the clock.
// So it is more useful for micro-architects than for measuring performance.
// The code is left here to avoid anyone else having to go through the
// experience of working all of that out!
#if (0 && defined(__clang__))
#if __has_builtin(__builtin_readcyclecounter)
#define USING_BUILTIN_CYCLECOUNT
inline auto readCycleCount() {
  return __builtin_readcyclecounter();
}
#endif
#endif

#if (LOMP_TARGET_ARCH_AARCH64 || LOMP_TARGET_ARCH_ARMV7L)
#include "target_arm.h"
#elif (LOMP_TARGET_ARCH_X86_64)
#include "target_x86_64.h"
#elif (LOMP_TARGET_ARCH_RISCV)
#include "target_riscv.h"
#else
#error "Unknown target architecture"
#endif

/* Allow the target architecture to set USE_YIELD, defaulting to don't */
#if (!USE_YIELD)
inline void Yield() {}
#endif

/* Similarly the architecture should set TARGET_HAS_CACHE_FLUSH if it does  */
#if (!TARGET_HAS_CACHE_FLUSH)
inline void FlushAddress(void *) {
#if (LOMP_WARN_ARCH_FEATURES)
#warning "The FlushAddress() function is a no-op on this architecture"
#endif
  /* do nothing for now */
}
#endif

#if (!TARGET_HAS_SPECULATION)
inline bool HaveSpeculation() {
  return false;
}
inline int32_t StartSpeculation() {
  return -1;
}
inline bool InSpeculation() {
  return false;
}
inline void CommitSpeculation() {}
#define Target_AbortSpeculation(tag) void(0)
#endif

#if (!TARGET_HAS_TIMESTAMP)
inline uint64_t readCycleCount() {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

constexpr double readHWTickTime() {
  auto sc = std::chrono::steady_clock::period();
  return double(sc.num) / double(sc.den);
}
#endif

// Return the CPU model name if we can find it.
std::string CPUModelName();

} // namespace Target

#define CACHE_ALIGNED alignas(CACHELINE_SIZE)

#include <atomic>
#include <complex>
// Type traits so that we can find signed, unsigned and equivalent width unsigned ints.
// of types. Maybe these should be elsewhere, since they're
// not really target dependent.
// pair_t is an integer wide enough to hold two of the type, useful when we need to
// use a compare exchange on a pair of them.
template <typename T>
struct typeTraits_t {
  static constexpr int type_size = sizeof(T);
};
// int8_t
template <>
struct typeTraits_t<int8_t> {
  typedef int8_t signed_t;
  typedef uint8_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint16_t pair_t;
};
// uint8_t
template <>
struct typeTraits_t<uint8_t> {
  typedef int8_t signed_t;
  typedef uint8_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint16_t pair_t;
};
// int16_t
template <>
struct typeTraits_t<int16_t> {
  typedef int16_t signed_t;
  typedef uint16_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint32_t pair_t;
};
// uint16_t
template <>
struct typeTraits_t<uint16_t> {
  typedef int16_t signed_t;
  typedef uint16_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint32_t pair_t;
};
// int32_t
template <>
struct typeTraits_t<int32_t> {
  typedef int32_t signed_t;
  typedef uint32_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint64_t pair_t;
};
// uint32_t
template <>
struct typeTraits_t<uint32_t> {
  typedef int32_t signed_t;
  typedef uint32_t unsigned_t;
  typedef unsigned_t uint_t;
  typedef uint64_t pair_t;
};
// int64_t
template <>
struct typeTraits_t<int64_t> {
  typedef int64_t signed_t;
  typedef uint64_t unsigned_t;
  typedef unsigned_t uint_t;
#if LOMP_HAVE_INT128T
  typedef __uint128_t pair_t;
#endif
};
// uint64_t
template <>
struct typeTraits_t<uint64_t> {
  typedef int64_t signed_t;
  typedef uint64_t unsigned_t;
  typedef unsigned_t uint_t;
#if LOMP_HAVE_INT128T
  typedef __uint128_t pair_t;
#endif
};
// For the float types we're really only interested in the
// integers, so that we can use CAS.
// float
template <>
struct typeTraits_t<float> {
  typedef int32_t signed_t;
  typedef uint32_t unsigned_t;
  typedef unsigned_t uint_t;
};
// double
template <>
struct typeTraits_t<double> {
  typedef int64_t signed_t;
  typedef uint64_t unsigned_t;
  typedef unsigned_t uint_t;
};
// complex<float>
template <>
struct typeTraits_t<std::complex<float>> {
  typedef int64_t signed_t;
  typedef uint64_t unsigned_t;
  typedef unsigned_t uint_t;
};

#if LOMP_HAVE_INT128T
// complex<double>
template <>
struct typeTraits_t<std::complex<double>> {
  typedef __int128_t signed_t;
  typedef __uint128_t unsigned_t;
  typedef unsigned_t uint_t;
};
#endif

#endif /* TARGET_H_INCLUDED */
