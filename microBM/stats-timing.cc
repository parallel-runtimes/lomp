//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/** @file stats_timing.h
 * Access to real time clock and timers, and general statistics support code.
 */

#include <algorithm> // for max and min
#include <iomanip>
#include <cstring>
#include <chrono>

#include "stats-timing.h"
#include "mlfsr32.h"

namespace Target {
/* CPU model name; here since we need to extract it anyway for Intel rdtsc time clock rate.
   */

#if (LOMP_TARGET_ARCH_X86_64)
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

// Despite Wikipedia's assertion that Rosetta returns "VirtualApple",
// what I see is "GenuineIntel", which seems rather dubious, however
// it does mean that existing binaries which are deciphering the
// Intel implementation of cpuid continue to work.
static bool onAppleRosetta() {
  // This should work, whereas the straighforward brand name check below does not.
  return Target::CPUModelName().find("Apple") != std::string::npos;
  // return CPUBrandName() == "VirtualApple";
}

std::string CPUModelName() {
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
    lomp::errPrintf("Beware: unknown CPU vendor; not sure how to read the "
                    "CPUModelName. Brand: '%s'\n",
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
#else
// May be able to get it from /proc/cpuinfo
// Though this doesn't get us anything useful on ARM, so maybe you
// you just have to know via magic.
std::string Target::CPUModelName() {
#if (LOMP_TARGET_LINUX)
  FILE * f = fopen("/proc/cpuinfo", "r");
  if (!f) {
    return std::string("UNKNOWN: cannot open /proc/cpuinfo");
  }
  char line[256];
  while (fgets(&line[0], sizeof(line), f) != 0) {
    if (strncmp("model name\t: ", &line[0], 13)) {
      fclose(f);
      return std::string(&line[13]);
    }
  }
  fclose(f);
  return std::string("UNKNOWN: failed to find 'model name :' in /proc/cpuinfo");
#else
  return std::string("Cannot find CPU model name.");
#endif /* LOMP_TARGET_LINUX */
}
#endif /* LOMP_TARGET_ARCH_X86_64 */
} // namespace Target

namespace lomp {

uint32_t randomExponentialBackoff::timeFactor =
    100.e-9 / tsc_tick_count::getTickTime();
uint32_t randomDelay::timeFactor =
    100.e-9 / lomp::tsc_tick_count::getTickTime();

/* Now the real statistics and timing code. */
double logHistogram::binMax[] = {
    1.e1l,  1.e2l,  1.e3l,  1.e4l,  1.e5l,  1.e6l,  1.e7l,  1.e8l,
    1.e9l,  1.e10l, 1.e11l, 1.e12l, 1.e13l, 1.e14l, 1.e15l, 1.e16l,
    1.e17l, 1.e18l, 1.e19l, 1.e20l, 1.e21l, 1.e22l, 1.e23l, 1.e24l,
    1.e25l, 1.e26l, 1.e27l, 1.e28l, 1.e29l, 1.e30l};

/* ************* statistic member functions ************* */

void statistic::addSample(double sample) {
  sample -= offset;
  DEBUG_ASSERT(std::isfinite(sample));

  double delta = sample - meanVal;

  sampleCount = sampleCount + 1;
  meanVal = meanVal + delta / sampleCount;
  m2 = m2 + delta * (sample - meanVal);

  minVal = std::min(minVal, sample);
  maxVal = std::max(maxVal, sample);
  if (collectingHist)
    hist.addSample(sample);
}

statistic & statistic::operator+=(const statistic & other) {
  if (other.sampleCount == 0)
    return *this;

  if (sampleCount == 0) {
    *this = other;
    return *this;
  }

  uint64_t newSampleCount = sampleCount + other.sampleCount;
  double dnsc = double(newSampleCount);
  double dsc = double(sampleCount);
  double dscBydnsc = dsc / dnsc;
  double dosc = double(other.sampleCount);
  double delta = other.meanVal - meanVal;

  // Try to order these calculations to avoid overflows. If this were Fortran,
  // then the compiler would not be able to re-order over brackets. In C++ it
  // may be legal to do that (we certainly hope it doesn't, and the C+ Programming
  // Language 2nd edition suggests it shouldn't, since it says that exploitation
  // of associativity can only be made if the operation really is associative
  // (which floating addition isn't...)).
  meanVal = meanVal * dscBydnsc + other.meanVal * (1 - dscBydnsc);
  m2 = m2 + other.m2 + dscBydnsc * dosc * delta * delta;
  minVal = std::min(minVal, other.minVal);
  maxVal = std::max(maxVal, other.maxVal);
  sampleCount = newSampleCount;
  if (collectingHist)
    hist += other.hist;

  return *this;
}

void statistic::scale(double factor) {
  minVal = minVal * factor;
  maxVal = maxVal * factor;
  meanVal = meanVal * factor;
  m2 = m2 * factor * factor;
  return;
}

std::string statistic::format(char unit, bool total) const {
  std::string result = formatSI(sampleCount, 9, ' ');

  if (sampleCount == 0) {
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    if (total)
      result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
  }
  else {
    result = result + std::string(", ") + formatSI(minVal, 9, unit);
    result = result + std::string(", ") + formatSI(meanVal, 9, unit);
    result = result + std::string(", ") + formatSI(maxVal, 9, unit);
    if (total)
      result =
          result + std::string(", ") + formatSI(meanVal * sampleCount, 9, unit);
    result = result + std::string(", ") + formatSI(getSD(), 9, unit);
  }
  return result;
}

/* ************* histogram member functions ************* */

// Lowest bin that has anything in it
int logHistogram::minBin() const {
  for (int i = 0; i < numBins; i++) {
    if (bins[i].count != 0)
      return i - logOffset;
  }
  return -logOffset;
}

// Highest bin that has anything in it
int logHistogram::maxBin() const {
  for (int i = numBins - 1; i >= 0; i--) {
    if (bins[i].count != 0)
      return i - logOffset;
  }
  return -logOffset;
}

// Which bin does this sample belong in ?
uint32_t logHistogram::findBin(double sample) {
  double v = std::fabs(sample);
  // Simply loop up looking which bin to put it in.
  // According to a micro-architect this is likely to be faster than a binary
  // search, since it will only have one branch mis-predict
  for (int b = 0; b < numBins; b++)
    if (binMax[b] > v)
      return b;
  fatalError("Trying to add a sample that is too large into a histogram\n");

  return -1;
}

void logHistogram::addSample(double sample) {
  if (sample == 0.0) {
    zeroCount += 1;
#ifdef DEBUG
    _total++;
    check();
#endif
    return;
  }
  DEBUG_ASSERT(std::isfinite(sample));
  uint32_t bin = findBin(sample);
  DEBUG_ASSERT(bin < numBins);

  bins[bin].count += 1;
  bins[bin].total += sample;
#ifdef DEBUG
  _total++;
  check();
#endif
}

// This may not be the format we want, but it'll do for now
std::string logHistogram::format(char unit) const {
  std::stringstream result;

  result << "Bin,                Count,     Total\n";
  if (zeroCount) {
    result << "0,              " << formatSI(zeroCount, 9, ' ') << ", ",
        formatSI(0.0, 9, unit);
    if (count(minBin()) == 0)
      return result.str();
    result << "\n";
  }
  for (int i = minBin(); i <= maxBin(); i++) {
    result << "10**" << i << "<=v<10**" << (i + 1) << ", "
           << formatSI(count(i), 9, ' ') << ", " << formatSI(total(i), 9, unit);
    if (i != maxBin())
      result << "\n";
  }

  return result.str();
}

/* Timing functions */
double tsc_tick_count::TickTime = 0.0;

#if (LOMP_TARGET_ARCH_AARCH64 || LOMP_TARGET_ARCH_ARMV7L)
static double readHWTickTime() {
  return 1. / double(Target::getHRFreq());
}
#elif (LOMP_TARGET_ARCH_X86_64)

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
  Target::cpuid_t cpuinfo;

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
  auto brandString = Target::CPUModelName();
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

static double measureTSCtickOnce() {
  // Use C++ "steady_clock" since cppreference.com recommends against
  // using hrtime.  Busy wait for 1ms based on the std::chrono clock
  // and time that with rdtsc.
  // Assuming the steady clock has a resonable resolution, 1ms should be
  // long enough to wait. At a 1GHz clock, that is still 1MT.
  auto start = std::chrono::steady_clock::now();
  tsc_tick_count startTick;
  auto end = start + std::chrono::milliseconds(1);

  while (std::chrono::steady_clock::now() < end) {
  }

  auto elapsed = tsc_tick_count::now() - startTick;
  // We should maybe read the actual end time here, but this seems
  // to work OK without that. (And, if we're worried about
  // our thread being stolen for a while, that could happen between the
  // rdtsc and reading this clock too...)
  double tickTime = 1.e-3 / elapsed.getValue();

  // errPrintf("Measured TSC tick as %s, frequency %sz\n",
  //           formatSI(tickTime, 6, 's').c_str(),
  //           formatSI(1 / tickTime, 6, 'H').c_str());
  return tickTime;
}

// Run the individual measurement five times and then take the shortest time.
// We can reasonably put up with 5ms of overhead once at startup, and this should overceom
// any real danger of this being interrupted and so misleading.
static double measureTSCtick() {
  double minTick = 1.0; /* We know it must be faster than one per second! */

  for (int i = 0; i < 5; i++) {
    double measurement = measureTSCtickOnce();
    minTick = measurement < minTick ? measurement : minTick;
  }

  return minTick;
}
static double readHWTickTime() {
  // First check whether TSC can sanely be used at all.
  // These leaves are common to Intel and AMD.
  Target::cpuid_t cpuinfo;
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
  if (Target::onIntel()) {
    // Try to get it from the brand name of the CPU (Intel have it there as a string),
    // and do seem to have the TSC clock run at the notional CPU frequency.
    if (readHWTickTimeFromName(&res)) {
      return res;
    }
  }
  // OK, we're on AMD or Apple emulation or..., all we can do is measure it.
  return measureTSCtick();
}
#else
static double readHWTickTime() {
#warning "Function readHWTickTime() is not implemented for this platform!"
  return 1.;
}
#endif

double tsc_tick_count::getTickTime() {
  if (UNLIKELY(TickTime == 0.0)) {
    TickTime = readHWTickTime();
  }
  return TickTime;
}

double tsc_tick_count::tsc_interval_t::seconds() const {
  return tsc_tick_count::getTickTime() * ticks();
}

static bool useSI = true;

// Return a formatted string after normalising the value into
// engineering style and using a suitable unit prefix (e.g. ms, us, ns).
::std::string formatSI(double interval, int width, char unit) {
  ::std::stringstream os;

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
      os << ::std::setw(width - 3) << ::std::right << "0.00" << ::std::setw(3)
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
        os << ::std::fixed << ::std::setprecision(2) << ::std::setw(width - 3)
           << ::std::right << (negative ? -interval : interval)
           << ::std::setw(2) << ranges[i].prefix << ::std::setw(1) << unit;

        return os.str();
      }
    }
  }
  os << ::std::setprecision(2) << ::std::fixed << ::std::right
     << ::std::setw(width - 3) << interval << ::std::setw(3) << unit;

  return os.str();
}
} // namespace lomp
