//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/** @file getClock.cc
 * Access to real time clock and timers, and general statistics support code.
 */

// Reduced code that extracts the time of the high resolution clock which can be accessed directly
// from user-space.
//
// This code should work on Intel, AMD, and Arm processors.
// The AMD style code should be easily portable elsewhere, of course if you're on a different
// architecture, you need to add access to the clock for that architecture too!
#if (defined(__aarch64__))
#define TARGET_AARCH64 1
#define TARGET_X86_64 0
#elif (defined(__x86_64__))
#define TARGET_AARCH64 0
#define TARGET_X86_64 1
#else
#error Unknown target architecture.
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>

// Error reporting and so on
// for stats/timing without requiring the whole runtime.
// Miscellaneous macros
#define LIKELY(cond) __builtin_expect(int(cond), 1)
#define UNLIKELY(cond) __builtin_expect(int(cond), 0)

[[noreturn]] void fatalError(char const * Format, ...);

static void eprintf(char const * tag, char const * Format, va_list & VarArgs,
                    bool Newline = true) {
  // Buffer to ensure a single write operation (though even that may not be atomic?)
  enum { DEBUG_BUFSZ = 4096 };
  char buffer[DEBUG_BUFSZ];
  size_t pos = 0;
  if (tag) {
    pos = strlen(tag);
    if (UNLIKELY(pos >= DEBUG_BUFSZ)) {
      fatalError("Prefix in eprintf is too long.");
      return;
    }
    strcpy(&buffer[0], tag);
  }

  pos += vsnprintf(&buffer[pos], DEBUG_BUFSZ - pos, Format, VarArgs);

  if ((pos < (DEBUG_BUFSZ - 1)) && Newline) {
    buffer[pos] = '\n';
    pos += 1;
  }
  // Ensure that we never write from beyond the buffer.
  pos = std::min(pos, size_t(DEBUG_BUFSZ));
  // Try to ensure a single write.
  // Writing to a stream (even stderr) need not be atomic.
  // Since this is for error messages we'd rather ensure we see them than
  // be efficient about how many system calls we make!
  write(STDERR_FILENO, &buffer[0], pos);
}

void errPrintf(char const * Format, ...) {
  va_list VarArgs;
  va_start(VarArgs, Format);
  eprintf(0, Format, VarArgs, false);
  va_end(VarArgs);
}

[[noreturn]] void fatalError(char const * Format, ...) {
  fflush(stdout);
  va_list VarArgs;
  va_start(VarArgs, Format);
  eprintf("***LOMP FATAL ERROR*** ", Format, VarArgs);
  va_end(VarArgs);
  exit(1);
}

static bool useSI = true;

// Return a formatted string after normalising the value into
// engineering style and using a suitable unit prefix (e.g. ms, us, ns).
std::string formatSI(double interval, int width, char unit) {
  std::stringstream os;

  if (useSI) {
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
  }
  os << std::setprecision(2) << std::fixed << std::right << std::setw(width - 3)
     << interval << std::setw(3) << unit;

  return os.str();
}

#if (TARGET_AARCH64)
// Setup functions we need for accessing the high resolution clock
#define GENERATE_READ_SYSTEM_REGISTER(ResultType, FuncName, Reg)               \
  inline ResultType FuncName() {                                               \
    uint64_t Res;                                                              \
    __asm__ volatile("mrs \t%0," #Reg : "=r"(Res));                            \
    return Res;                                                                \
  }

GENERATE_READ_SYSTEM_REGISTER(uint64_t, readCycleCount, cntvct_el0)
GENERATE_READ_SYSTEM_REGISTER(uint32_t, getHRFreq, cntfrq_el0)
// Tidiness; delete the macro once we're done with it.
#undef GENERATE_READ_SYSTEM_REGISTER

// It's easy on Arm :-)
static double readHWTickTime() {
  return 1. / double(getHRFreq());
}

#elif (TARGET_X86_64)
#include <x86intrin.h>
inline auto readCycleCount() {
  return __rdtsc();
}
#endif

static double measureTSCtick() {
  // Use C++ "steady_clock" since cppreference.com recommends against
  // using hrtime.  Busy wait for 1ms based on the std::chrono clock
  // and time that with rdtsc.
  // Assuming the steady clock has a resonable resolution, 1ms should be
  // long enough to wait. At a 1GHz clock, that is still 1MT.
  auto start = std::chrono::steady_clock::now();
  auto startTick = readCycleCount();
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
#if (TARGET_X86_64)
/* CPU model name; here since we need to extract it anyway for Intel rdtsc time clock rate.
 */
struct cpuid_t {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;

#if (DEBUG)
  std::string format() const {
    char buffer[64];
    snprintf(&buffer[0], sizeof(buffer),
             "eax: 0x%08x, ebx: 0x%08x, ecx: 0x%08x, edx: 0x%08x", eax, ebx,
             ecx, edx);
    return std::string(&buffer[0]);
  }
#endif
};

static inline void x86_cpuid(int leaf, int subleaf, struct cpuid_t * p) {
  __asm__ __volatile__("cpuid"
                       : "=a"(p->eax), "=b"(p->ebx), "=c"(p->ecx), "=d"(p->edx)
                       : "a"(leaf), "c"(subleaf));
#if (DEBUG == 2)
  errPrintf("cpuid: request leaf: 0x%08x, subleaf: 0x%08x\n"
            "       reply   %s\n",
            leaf, subleaf, p->format().c_str());
#endif
}

static std::string CPUBrandName() {
  cpuid_t cpuinfo;
  uint32_t intBuffer[4];
  char * buffer = (char *)&intBuffer[0];

  x86_cpuid(0x00000000, 0, &cpuinfo);
  int * bufferAlias = (int *)&buffer[0];
  intBuffer[0] = cpuinfo.ebx;
  intBuffer[1] = cpuinfo.edx;
  intBuffer[2] = cpuinfo.ecx;
  buffer[12] = char(0);

  return std::string(buffer);
}

static bool onIntel() {
  // N.B. Apple Rosetta 2 also claims to be GenuineIntel...
  return CPUBrandName() == "GenuineIntel";
}

static bool onAMD() {
  return CPUBrandName() == "AuthenticAMD";
}

static std::string CPUModelName();

// Despite Wikipedia's assertion that this is what Rosetta returns,
// what I see is "GenuineIntel", which seems rather dubious IMO.
static bool onAppleRosetta() {
  // This should work, whereas the straighforward brand name check below does not.
  return CPUModelName().find("Apple") != std::string::npos;
  // return CPUBrandName() == "VirtualApple";
}

// This does more than we need simply to extract the invariant TSC
// rate, but is useful anyway. We can get the TSC rate from here on Intel
// if we can't find it anywhere else.
static std::string CPUModelName() {
  cpuid_t cpuinfo;
  char brand[256];
  memset(&brand[0], 0, sizeof(brand));

  auto ids = 0;
  if (onAMD()) {
    // AMD always support three leaves here.
    ids = 3;
  }
  else if (onIntel()) {
    x86_cpuid(0x80000000, 0, &cpuinfo);
    // On Intel this gives the number of extra fields to read
    ids = cpuinfo.eax ^ 0x80000000;
  }
  else {
    std::string brandName = CPUBrandName();
    errPrintf("Unknown CPU vendor; not sure how to read the CPUModelName. "
              "Brand: '%s'\n",
              brandName.c_str());
    return brandName;
  }

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
#if (DEBUG)
    errPrintf(
        "Cannot use cpuid leaf 0x15 to find clock time; max leaf is 0x%02x\n",
        cpuinfo.eax);
#endif
    return false;
  }

  // Then read it if it does and check the results for sanity.
  x86_cpuid(0x15, 0, &cpuinfo);
  if (cpuinfo.ebx == 0 || cpuinfo.ecx == 0) {
#if (DEBUG)
    errPrintf("cpuid node 15H does not give frequency.\n");
#endif
    return false;
  }
  double coreCrystalFreq = cpuinfo.ecx;
  *time = cpuinfo.eax / (cpuinfo.ebx * coreCrystalFreq);
#if (DEBUG)
  errPrintf("Compute rdtsc tick time: coreCrystal = %g, eax=%u, ebx=%u, ecx=%u "
            "=> %5.2fps\n",
            coreCrystalFreq, cpuinfo.eax, cpuinfo.ebx, cpuinfo.ecx,
            (*time) * 1.e12);
#endif
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

  if (onIntel() && !onAppleRosetta()) {
    // Try to get it from the brand name of the CPU (Intel have it there as a string),
    // and do seem to have the TSC clock run at the notional CPU frequency.
    if (readHWTickTimeFromName(&res)) {
#if (DEBUG)
      errPrintf("Read clock time from CPU name\n");
#endif
      return res;
    }
  }

  // OK, we don't have a good way to get it, other than looking at time passing.
#if (DEBUG)
  errPrintf("Counting ticks in a fixed time\n");
#endif
  return measureTSCtick();
}
#endif

int main(int argc, char ** argv) {

  double tick = readHWTickTime();

#if (TARGET_X86_64)
  auto model = CPUModelName();
  auto brand = CPUBrandName();

  printf("%s: %s\n", brand.c_str(), model.c_str());
#endif
  auto measuredTick = measureTSCtick();
  printf("Measured tick = %s (%sz)\n", formatSI(measuredTick, 6, 's').c_str(),
         formatSI(1 / measuredTick, 6, 'H').c_str());

  printf("System announced tick = %s (%sz)\n", formatSI(tick, 6, 's').c_str(),
         formatSI(1 / tick, 6, 'H').c_str());
  return 0;
}
