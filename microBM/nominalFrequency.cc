//===-- microBM/nominalFrequency.cc - Print nominal CPU frequency  -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file looks at machine specific information to extract what we hope is the
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
#include <cstring>
#include <string>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>

// Code here is replicated from ../src/target.h just to make this
// easier to distribute as a single file.
#if (__x86_64__)
#define LOMP_TARGET_ARCH_X86_64 1
#elif (__aarch64__)
#define LOMP_TARGET_ARCH_AARCH64 1
#else
#error "Unknown target architecture"
#endif

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
#undef GENERATE_READ_SYSTEM_REGISTER

static double readHWTickTime() {
  return 1. / double(getHRFreq());
}
#elif (LOMP_TARGET_ARCH_X86_64)
#include <x86intrin.h>
inline auto readCycleCount() {
  return __rdtsc();
}
#endif

static double measureTSCtick() {
  // Use C++ "steady_clock" since cppreference.com recommends against
  // using hrtime.  Busy wait for 5ms based on the std::chrono clock
  // and time that with our high reolution low overhead clock.
  // Assuming the steady clock has a resonable resolution, 5ms should be
  // long enough to wait. At a 1GHz clock, that is still 5MT, and even at
  // a 1MHz clock it's 5kT.
  auto start = std::chrono::steady_clock::now();
  uint64_t startTick = readCycleCount();
  auto end = start + std::chrono::milliseconds(5);

  while (std::chrono::steady_clock::now() < end) {
  }

  auto elapsed = readCycleCount() - startTick;
  // We should maybe read the actual end time here, but this seems
  // to work OK without that. (And, if we're worried about
  // our thread being stolen for a while, that could happen between the
  // rdtsc and reading this clock too...)
  double tickTime = 5.e-3 / elapsed;

  // errPrintf("Measured TSC tick as %s, frequency %sz\n",
  //           formatSI(tickTime, 6, 's').c_str(),
  //           formatSI(1 / tickTime, 6, 'H').c_str());
  return tickTime;
}


#if(LOMP_TARGET_ARCH_X86_64)
[[noreturn]] static void fatalError(char const * Format, ...) {
  fflush(stdout);
  va_list VarArgs;
  va_start(VarArgs, Format);
  vfprintf(stderr, Format, VarArgs);
  exit(1);
}

/* cpuid fun. Here since we need to check the sanity of the time-stamp-counter.
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

static std::string CPUBrandName() {
  cpuid_t cpuinfo;
  uint32_t intBuffer[4];
  char * buffer = (char *)&intBuffer[0];

  // All of the X86 vendors agree on this leaf.
  // But, what you read here then determnines how you should interpret
  // other leaves.
  x86_cpuid(0x00000000, 0, &cpuinfo);
  int * bufferAlias = (int *)&buffer[0];
  intBuffer[0] = cpuinfo.ebx;
  intBuffer[1] = cpuinfo.edx;
  intBuffer[2] = cpuinfo.ecx;
  buffer[12] = char(0);

  return buffer;
}

static bool haveInvariantTSC() {
  // These leaves are common to Intel and AMD.
  cpuid_t cpuinfo;
  // Does the leaf that can tell us that exist?
  x86_cpuid(0x80000000, 0, &cpuinfo);
  if (cpuinfo.eax < 0x80000007) {
    // This processor cannot even tell us whether it has invariantTSC!
    return false;
  }
  // At least the CPU can tell us whether it supports an invariant TSC.
  x86_cpuid(0x80000007, 0, &cpuinfo);
  return (cpuinfo.edx & (1 << 8)) != 0;
}


static std::string CPUModelName() {
  cpuid_t cpuinfo;
  auto brand = CPUBrandName();
  int ids;
  
  if (brand == "GenuineIntel") {
    // On Intel this gives the number of extra fields to read.
    x86_cpuid(0x80000000, 0, &cpuinfo);
    ids = cpuinfo.eax ^ 0x80000000;
  } else if (brand == "AuthenticAMD") {
    // Whereas AMD always support exactly three extra fields.
    ids = 3;
  } else {
    fatalError("Unknown brand: %s", brand.c_str());
  }

  char model[256];
  memset(&model[0], 0, sizeof(model));

  for (unsigned int i = 0; i < ids; i++)
    x86_cpuid(i + 0x80000002, 0, (cpuid_t *)(model + i * sizeof(cpuid_t)));
  // Remove trailing blanks.
  char * start = &model[0];
  for (char * end = &start[strlen(start) - 1]; end > start && *end == ' ';
       end--) {
    *end = char(0);
  }

  // Remove leading blanks
  for (; *start == ' '; start++)
    ;

  // errPrintf("CPU model name from cpuid: '%s'\n", start);
  return start;
}

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
    printf ("   cpuid leaf 15H is not supported\n");
    return false;
  }

  // If it exists, check the results for sanity.
  x86_cpuid(0x15, 0, &cpuinfo);
  if (cpuinfo.ebx == 0 || cpuinfo.ecx == 0) {
    printf("    cpuid leaf 15H does not give frequency.\n");
    return false;
  }
  double coreCrystalFreq = cpuinfo.ecx;
  *time = cpuinfo.eax / (cpuinfo.ebx * coreCrystalFreq);
  printf("   cpuid leaf 15H: coreCrystal = %g, eax=%u, ebx=%u, ecx=%u "
         "=> %5.2fps\n",
         coreCrystalFreq, cpuinfo.eax, cpuinfo.ebx, cpuinfo.ecx,
         (*time) * 1.e12);
  return true;
}

// Try to extract it from the brand string.
static bool readHWTickTimeFromName(double * time) {
  auto modelName = CPUModelName();

  // Apple announce the CPU with a clock rate, but it's not the
  // rate at which the emulated rdtsc ticks...
  if (modelName.find("Apple") != std::string::npos) {
    return false;
  }
  
  char const * model = modelName.c_str();
  auto end = model + strlen(model) - 3;
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
  while (*end != ' ' && end >= model)
    end--;
  char * uninteresting;
  double freq = strtod(end + 1, &uninteresting);
  if (freq == 0.0) {
    return false;
  }

  *time = ((double)1.0) / (freq * multiplier);
  printf("   read TSC tick time from %s\n", model);
  return true;
}

static double readHWTickTime() {
  // First check whether TSC can sanely be used at all.
  if (!haveInvariantTSC()) {
    fatalError("TSC may not be invariant. Use another clock!");
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

  // OK, we can't find it, so we have to measure.
  return measureTSCtick();
}
#endif

// Try to see whether the clock actually ticks at the same rate as its value is enumerated in.
// Consider a clock whose value is enumerated in seconds, but which only changes once an hour...
// Just because a clock has a fine interval, that doesn't mean it can measure to that level.
static uint64_t measureClockGranularity() {
  // If the clock is very slow, this might not work...
  uint64_t delta = std::numeric_limits<uint64_t>::max();

  for (int i = 0; i < 50; i++) {
    uint64_t m1 = readCycleCount();
    uint64_t m2 = readCycleCount();
    uint64_t m3 = readCycleCount();
    uint64_t m4 = readCycleCount();
    uint64_t m5 = readCycleCount();
    uint64_t m6 = readCycleCount();
    uint64_t m7 = readCycleCount();
    uint64_t m8 = readCycleCount();
    uint64_t m9 = readCycleCount();
    uint64_t m10 = readCycleCount();

    auto d = (m2 - m1);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m3 - m2);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m4 - m3);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m5 - m4);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m6 - m5);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m7 - m6);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m8 - m7);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m9 - m8);
    if (d != 0)
      delta = std::min(d, delta);
    d = (m10 - m9);
    if (d != 0)
      delta = std::min(d, delta);
  }

  return delta;
}

// Return a formatted string after normalising the value into
// engineering style and using a suitable unit prefix (e.g. ms, us, ns).
std::string formatSI(double interval, int width, char unit) {
  std::stringstream os;

  // Preserve accuracy for small numbers, since we only multiply and the
  // positive powers of ten are precisely representable.
  static struct {
    double scale;
    char prefix;
  } ranges[] = {{1.e21, 'y'},  {1.e18, 'z'},  {1.e15, 'a'},  {1.e12, 'f'},
                {1.e9, 'p'},   {1.e6, 'n'},   {1.e3, 'u'},   {1.0, 'm'},
                {1.e-3, ' '},  {1.e-6, 'k'},  {1.e-9, 'M'},  {1.e-12, 'G'},
                {1.e-15, 'T'}, {1.e-18, 'P'}, {1.e-21, 'E'}, {1.e-24, 'Z'},
                {1.e-27, 'Y'}};

  if (interval == 0.0) {
    os << std::setw(width - 3) << std::right << "0.00" << std::setw(3)
       << unit;
    return os.str();
  }

  bool negative = false;
  if (interval < 0.0) {
    negative = true;
    interval = -interval;
  }

  for (int i = 0; i < (int)(sizeof(ranges) / sizeof(ranges[0])); i++) {
    if (interval * ranges[i].scale < 1.e0) {
      interval = interval * 1000.e0 * ranges[i].scale;
      os << std::fixed << std::setprecision(2) << std::setw(width - 3)
         << std::right << (negative ? -interval : interval) << std::setw(2)
         << ranges[i].prefix << std::setw(1) << unit;

      return os.str();
    }
  }
  os << std::setprecision(2) << std::fixed << std::right << std::setw(width - 3)
     << interval << std::setw(3) << unit;

  return os.str();
}

int main(int, char **) {
#if (LOMP_TARGET_ARCH_AARCH64)
  double res = readHWTickTime();
  
  printf("AArch64 processor: \n"
          "   From high resolution timer frequency (cntfrq_el0) "
         "%sz => %s\n",
         formatSI(1./res,9,'H').c_str(), formatSI(res,9,'s').c_str());
#elif (LOMP_TARGET_ARCH_X86_64)
  std::string brandName = CPUBrandName();
  std::string modelName = CPUModelName();
  bool invariant = haveInvariantTSC();

  printf("x86_64 processor:\n   Brand: %s\n   Model: %s\n", brandName.c_str(), modelName.c_str());
  printf("   Invariant TSC: %s\n", invariant ? "True" : "False");
  if (!invariant) {
    printf ("*** Without invariant TSC rdtsc is not a useful timer for wall clock time.\n");
    return 1;
  }
  char const * source = "Unknown";
  double res;
  // Try to get it from Intel's leaf15H
  if (extractLeaf15H(&res)) {
    source = "leaf 15H";
  } else if (readHWTickTimeFromName(&res)) {
    source = "model name string";
  } else {
      res = measureTSCtick();
      source = "measurement";
  }

  printf ("   From %s frequency %sz => %s\n",
          source,
          formatSI(1./res,9,'H').c_str(), formatSI(res,9,'s').c_str());
#endif
  // Check it...
  double measured = measureTSCtick();
  printf ("\nSanity check against std::chrono::steady_clock gives frequency %sz => %s\n",
          formatSI(1./measured,9,'H').c_str(), formatSI(measured,9,'s').c_str());
  uint64_t minTicks = measureClockGranularity();
  res = res*minTicks;
  printf ("Measured granularity = %llu tick%s => %sz, %s\n",
          (unsigned long long) minTicks, minTicks != 1 ? "s": "", formatSI(1./res,9,'H').c_str(), formatSI(res,9,'s').c_str());

  return 0;
}
