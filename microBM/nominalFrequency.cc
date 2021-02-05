//===-- microBM/nominalFrequency.cc - Print nominal CPU frequency  -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file loks at machie specific information to extract what we hope is the
/// nominal CPU frequency of the core.
/// On Intel cores that is included in the brand name extracted via cpuid
/// On AMD cores it is not there and we have to measure it
/// On Arm cores we can read it from a system register.
/// Note that what we're actually looking at is the frequency at which the
/// user-accessible high resolution timer runs. (That returned by rdtsc on x86_64,
// and by reading the cntvct_el0 register on aarch64).
///
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <string>
#include <chrono>

#include <string.h>
#include <stdarg.h>

// Code here is replicated from ../src/target.h just to make this
// easier to distribute as a single file.
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

#if (LOMP_TARGET_ARCH_AARCH64 || LOMP_TARGET_ARCH_ARMV7L)

// Setup functions we need for accessing the high resolution clock
#if (LOMP_TARGET_ARCH_AARCH64)
#define GENERATE_READ_SYSTEM_REGISTER(ResultType, FuncName, Reg)               \
  inline ResultType FuncName() {                                               \
    uint64_t Res;                                                              \
    __asm__ volatile("mrs \t%0," #Reg : "=r"(Res));                            \
    return Res;                                                                \
  }

GENERATE_READ_SYSTEM_REGISTER(uint64_t, readCycleCount, cntvct_el0)
GENERATE_READ_SYSTEM_REGISTER(uint32_t, getHRFreq, cntfrq_el0)

#endif // LOMP_TARGET_ARCH_AARCH64

#if (LOMP_TARGET_ARCH_ARMV7L)
#undef GENERATE_READ_SYSTEM_REGISTER

inline uint32_t getHRFreq() {
#if LOMP_WARN_ARCH_FEATURES
#warning "The getHRFreq() function does not return useful values for armv7l!"
#endif
  return 1;
}

inline uint64_t readCycleCount() {
#if LOMP_WARN_ARCH_FEATURES
#warning                                                                       \
    "The readCycleCount() function does not return useful values for armv7l!"
#endif
  return 1;
}

#endif // LOMP_TARGET_ARCH_ARMV7L

static double readHWTickTime() {
  return 1. / double(getHRFreq());
}

// Tidiness for those including this
#undef GENERATE_READ_SYSTEM_REGISTER

#elif (LOMP_TARGET_ARCH_RISCV)

inline uint32_t getHRFreq() {
#if LOMP_WARN_ARCH_FEATURES
#warning "The getHRFreq() function does not return useful values for riscv!"
#endif
  return 1;
}

inline uint64_t readCycleCount() {
#if LOMP_WARN_ARCH_FEATURES
#warning                                                                       \
    "The readCycleCount() function does not return useful values for riscv!"
#endif
  return 1;
}

static double readHWTickTime() {
#if LOMP_WARN_ARCH_FEATURES
#warning                                                                       \
    "The readHWTickTime() function does not return useful values for riscv!"
#endif
  return 1. / double(getHRFreq());
}

#elif (LOMP_TARGET_ARCH_X86_64)

#include <x86intrin.h>
inline auto readCycleCount() {
  return __rdtsc();
}

[[noreturn]] static void fatalError(char const * Format, ...) {
  fflush(stdout);
  va_list VarArgs;
  va_start(VarArgs, Format);
  vfprintf(stderr, Format, VarArgs);
  exit(1);
}

/* CPU model name; here since we need to extract it anyway for Intel rdtsc time clock rate.
   */
struct cpuid_t {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
};

static inline void x86_cpuid(int leaf, int subleaf, struct cpuid_t * p) {
  __asm__ __volatile__("cpuid"
                       : "=a"(p->eax), "=b"(p->ebx), "=c"(p->ecx), "=d"(p->edx)
                       : "a"(leaf), "c"(subleaf));
}

std::string CPUModelName() {
  cpuid_t cpuinfo;

  x86_cpuid(0x80000000, 0, &cpuinfo);
  // On Intel this gives the number of extra fields to read
  auto ids = cpuinfo.eax ^ 0x80000000;
  // 0x68747541 == "htuA" == "Auth" in little endian;
  // the first part of "Authentic AMD"
  if (cpuinfo.ebx == 0x68747541) {
    // AMD always support three leaves here.
    ids = 3;
  }
  char brand[256];
  memset(&brand[0], 0, sizeof(brand));

  for (unsigned int i = 0; i < ids; i++)
    x86_cpuid(i + 0x80000002, 0, (cpuid_t *)(brand + i * sizeof(cpuid_t)));
  // Remove trailing blanks.
  char * start = &brand[0];
  for (char * end = &start[strlen(start) - 1]; end > start && *end == ' ';
       end--) {
    *end = char(0);
  }

  // Remove leading blanks
  for (; *start == ' '; start++)
    ;

  // errPrintf("CPU model name from cpuid: '%s'\n", start);
  return std::string(start);
}

/* Timing functions */
// Extract the value from CPUID information; this is not entirely trivial!
static bool extractLeaf15H(double * time) {
  // From Intel PRM:
  // Intel Cpuid leaf  15H
  // If EBX[31:0] is 0, the TSC/”core crystal clock” ratio is not enumerated.
  // EBX[31:0]/EAX[31:0] indicates the ratio of the TSC frequency and the core crystal clock frequency.
  // If ECX is 0, the nominal core crystal clock frequency is not enumerated.
  // “TSC frequency” = “core crystal clock frequency” * EBX/EAX.
  // The core crystal clock may differ from the reference clock, bus clock, or core clock frequencies.
  // EAX Bits 31 - 00: An unsigned integer which is the denominator of the TSC/”core crystal clock” ratio.
  // EBX Bits 31 - 00: An unsigned integer which is the numerator of the TSC/”core crystal clock” ratio.
  // ECX Bits 31 - 00: An unsigned integer which is the nominal frequency of the core crystal clock in Hz.
  // EDX Bits 31 - 00: Reserved = 0.
  cpuid_t cpuinfo;

  // Check whether the leaf even exists
  x86_cpuid(0x0, 0, &cpuinfo);
  if (cpuinfo.eax < 0x15) {
    return false;
  }

  // Then read it if it does and check the results for sanity.
  x86_cpuid(0x15, 0, &cpuinfo);
  if (cpuinfo.ebx == 0 || cpuinfo.ecx == 0) {
    // errPrintf("cpuid node 15H does not give frequency.\n");
    return false;
  }
  double coreCrystalFreq = cpuinfo.ecx;
  *time = cpuinfo.eax / (cpuinfo.ebx * coreCrystalFreq);
  // errPrintf("Compute rdtsc tick time: coreCrystal = %g, eax=%u, ebx=%u, ecx=%u "
  //           "=> %5.2fps\n",
  //           coreCrystalFreq, cpuinfo.eax, cpuinfo.ebx, cpuinfo.ecx,
  //           (*time) * 1.e12);
  return true;
}

// Try to extract it from the brand string.
static bool readHWTickTimeFromName(double * time) {
  auto brandString = CPUModelName();
  char const * brand = brandString.c_str();
  auto end = brand + strlen(brand) - 3;
  uint64_t multiplier;

  if (*end == 'M')
    multiplier = 1000LL * 1000LL;
  else if (*end == 'G')
    multiplier = 1000LL * 1000LL * 1000LL;
  else if (*end == 'T')
    multiplier = 1000LL * 1000LL * 1000LL * 1000LL;
  else {
    return false;
  }
  while (*end != ' ' && end >= brand)
    end--;
  char * uninteresting;
  double freq = strtod(end + 1, &uninteresting);
  if (freq == 0.0) {
    return false;
  }

  *time = ((double)1.0) / (freq * multiplier);
  // errPrintf("Computed TSC tick time %s from %s\n", formatSI(*time,6,'s').c_str(),brand);
  return true;
}

static double measureTSCtick() {
  // Use C++ "steady_clock" since cppreference.com recommends against
  // using hrtime.  Busy wait for 1ms based on the std::chrono clock
  // and time that with rdtsc.
  // Assuming the steady clock has a resonable resolution, 1ms should be
  // long enough to wait. At a 1GHz clock, that is still 1MT.
  auto start = std::chrono::steady_clock::now();
  uint64_t startTick = readCycleCount();
  auto end = start + std::chrono::milliseconds(1);

  while (std::chrono::steady_clock::now() < end) {
  }

  auto elapsed = readCycleCount() - startTick;
  // We should maybe read the actual end time here, but this seems
  // to work OK without that. (And, if we're worried about
  // our thread being stolen for a while, that could happen between the
  // rdtsc and reading this clock too...)
  double tickTime = 1.e-3 / elapsed;

  // errPrintf("Measured TSC tick as %s, frequency %sz\n",
  //           formatSI(tickTime, 6, 's').c_str(),
  //           formatSI(1 / tickTime, 6, 'H').c_str());
  return tickTime;
}

static double readHWTickTime() {
  // First check whether TSC can sanely be used at all.
  // These leaves are common to Intel and AMD.
  cpuid_t cpuinfo;
  // Does the leaf that can tell us that exist?
  x86_cpuid(0x80000000, 0, &cpuinfo);
  if (cpuinfo.eax < 0x80000007) {
    fatalError(
        "This processor cannot even tell us whether it has invariantTSC!");
  }
  // At least the CPU can tell us whether it supports an invariant TSC.
  x86_cpuid(0x80000007, 0, &cpuinfo);
  if ((cpuinfo.edx & (1 << 8)) == 0) {
    fatalError("This processor does not have invariantTSC.");
  }
  double res;
  // Try to get it from Intel's leaf15H
  if (extractLeaf15H(&res)) {
    return res;
  }
  // Try to get it from the brand name of the CPU (Intel have it there as a string),
  // and do seem to have the TSC clock run at the notional CPU frequency.
  if (readHWTickTimeFromName(&res)) {
    return res;
  }

  // OK, maybe we're on AMD; we could check, but there doesn't seem
  // much point, really, since what we end up doing is vendor (and
  // architecture) independent.
  return measureTSCtick();
}

#else
#error "Unknown target architecture.  Cannot compile this file."
#endif

int main(int, char **) {
  double freq = 1.e-9 / readHWTickTime();

#if (LOMP_TARGET_ARCH_AARCH64 || LOMP_TARGET_ARCH_ARMV7L)
  // On Arm the generic counter frequency is unrelated to the CPU's nominal clock rate.
  char tag;
  if (freq > 1.0) {
    tag = 'G';
  }
  else {
    freq = freq * 1000;
    tag = 'M';
  }
  printf("Arm processor: high resolution timer frequency (cntfrq_el0) = "
         "%6.3f%cHz\n",
         freq, tag);
#elif (LOMP_TARGET_ARCH_X86_64)
  std::string brandName = CPUModelName();
  printf("%s: nominal clock frequency %6.3fGHz\n", brandName.c_str(), freq);
#elif (LOMP_TARGET_ARCH_RISCV)
#if (LOMP_WARN_ARCH_FEATURES)
#warning "Need to implement code in main to show frequency on riscv."
#endif
#else
#error "Unknown target architecture.  Cannot compile this file."
#endif

  return 0;
}
