//===-- stats-timing.cc - Timers and statistics -----------------*- C++ -*-===//
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
#include <sstream>
#include <limits>

#include "target.h"
#if (LOMP_TARGET_MACOS)
#include <sys/sysctl.h>
#endif
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

  intBuffer[0] = cpuinfo.ebx;
  intBuffer[1] = cpuinfo.edx;
  intBuffer[2] = cpuinfo.ecx;
  buffer[12] = char(0);

  return buffer;
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
  // This works, whereas a straightforward CPUBrandName check will not.
  return Target::CPUModelName().find("Apple") != std::string::npos;
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

  for (int i = 0; i < ids; i++)
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
// Not on an X86...
// May be able to get it from /proc/cpuinfo
std::string CPUModelName() {
#if (LOMP_TARGET_LINUX)
  FILE * f = fopen("/proc/cpuinfo", "r");
  if (f) {
    char line[256];
    while (fgets(&line[0], sizeof(line), f) != 0) {
      if (strncmp("model name\t: ", &line[0], 13) == 0) {
	fclose(f);
	return std::string(&line[13]);
      }
    }
    fclose(f);
  }
#elif (LOMP_TARGET_MACOS)
  char buffer[64]; /* Should be long enough! */
  size_t len = sizeof(buffer);

  if (sysctlbyname("machdep.cpu.brand_string", &buffer[0], &len, 0, 0) == 0) {
    return std::string(&buffer[0]);
  }
  else {
    return LOMP_TARGET_ARCH_NAME;
  }
#endif /* Operating systems */
#if (LOMP_TARGET_ARCH_AARCH64)  
  // Try the AArch64 MIDR_EL1 register; we might be able to work it out from there.
  // https://developer.arm.com/documentation/ddi0595/2020-12/AArch64-Registers/MIDR-EL1--Main-ID-Register
  struct EL1 {
    unsigned int revision : 4;
    unsigned int partNum : 12;
    unsigned int architecture : 4;
    unsigned int variant : 4;
    unsigned int implementer : 8;
  };
  // Use a union to avoid undefined behaviour complaints about
  // aliasing that come from pointer casts.
  union {
    uint32_t rawReg;
    EL1 el1Reg;
  } el1;
  el1.rawReg = getArmID();
  uint32_t implementer = el1.el1Reg.implementer;
  uint32_t partNum = el1.el1Reg.partNum;
  std::string name;
  // We only know about a few things here. More code is potentially needed.
  switch (implementer) {
  case 0x43:
    if (partNum == 0xaf) {
      // 0xaf == Cavium, but they're now Marvell
      return "Marvell ThunderX2";
    }
    return "Unknown Cavium CPU";
  case 0x46:
    if (partNum == 1) {
      return "Fujitsu A64FX";
    }
    return "Unknown Fujitsu CPU";

  default:
    return LOMP_TARGET_ARCH_NAME " Unknown implementer";
  }
#else
  // Add architecture specific code here if you can find appropriate things to use.
  // On Arm32, there are some registers, but they're not accessible to unprivileged code.
  // https://developer.arm.com/documentation/ddi0406/b/System-Level-Architecture/The-CPUID-Identification-Scheme/Introduction-to-the-CPUID-scheme/General-features-of-the-CPUID-registers?lang=en

  // RISC-V may also have some trickery; Google Is Your Friend...
  return LOMP_TARGET_ARCH_NAME;
#endif  
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
/* Since we are trying to use the highest resolution, lowest overhead, timer which we
 * can, this is target architecture dependent.
 * On some architectures, such as AARCH64 it is relatively easy; on others, such as X86_64,
 * there is a degree of complexity involved.
 */
double tsc_tick_count::TickTime = 0.0;
std::string tsc_tick_count::Description = "";

// Try to see whether the clock actually ticks at the same rate as its value is enumerated in.
// Consider a clock whose value is enumerated in seconds, but which only changes once an hour...
// Just because a clock has a fine interval, that doesn't mean it can measure to that level.
static int64_t getClockGranularity() {
  // If the clock is very slow, this might not work...
  int64_t delta = std::numeric_limits<int64_t>::max();

  for (int i = 0; i < 10; i++) {
    tsc_tick_count m1;
    tsc_tick_count m2;
    tsc_tick_count m3;
    tsc_tick_count m4;
    tsc_tick_count m5;
    tsc_tick_count m6;
    tsc_tick_count m7;
    tsc_tick_count m8;
    tsc_tick_count m9;
    tsc_tick_count m10;

    auto d = (m2 - m1).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m3 - m2).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m4 - m3).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m5 - m4).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m6 - m5).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m7 - m6).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m8 - m7).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m9 - m8).getValue();
    if (d != 0)
      delta = std::min(d, delta);
    d = (m10 - m9).getValue();
    if (d != 0)
      delta = std::min(d, delta);
  }

  return delta;
}

static std::string formatTimer(double tick) {
  std::stringstream desc;
  auto delta = getClockGranularity();

  desc << "- tick " << formatSI(tick, 5, 's') << " ("
       << formatSI(1. / (tick), 5, 'H') << "z) delta " << delta << " T";
  return desc.str();
}

#if (TARGET_HAS_TIMESTAMP)
#if (LOMP_TARGET_ARCH_AARCH64)
// Nice and simple!
static double readHWTickTime() {
  double tick = 1. / double(Target::getHRFreq());
  std::stringstream desc;
  desc << "AARCH64 cntvct_el0 " << formatTimer(tick) << " from cntfreq_el0";
  tsc_tick_count::setDescription(desc.str());

  return tick;
}
#elif (LOMP_TARGET_ARCH_RISCV)
constexpr double readHWTickTime() {
  auto sc = std::chrono::steady_clock::period();
  return double(sc.num) / double(sc.den);
}
#elif (LOMP_TARGET_ARCH_X86_64)
// Extract the tcik time from CPUID information; this is way more complicated than you would hope,
// and depends on the hardware vendor.
//
// For Intel there are two ways to do it :-
// 1) Use cpuid leaf 15h. This is best, but many cores don't yet have leaf 15h.
// 2) Read it from the CPU Model name (extracted from cpuid). Intel put the nominal frequency in the name,
//    so we can parse that.
//
// For AMD processors there seems to be no way to find this.
// They do not support leaf 15h and do not give the nominal frequency in the brand name.
// So we have to look at how many rdtsc ticks pass in a known amount of time (using std::chrono::steady_clock for
// the fixed time).
//
// For Apple emulation (on their M1 Arm, at least),
// 1) They claim to be a "GenuineIntel" processor (possibly legally dubious!)
// 2) They don't support leaf 15h.
// 3) They do give a nominal frequency in the brand string, *BUT* that doesn't give the emulated rdtsc
//    tick period.
// So we have to recognize that we're on an Apple emulation and use the "measure it" approach.
//

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

  std::stringstream desc;
  desc << "X86 TSC " << formatTimer(*time) << " from cpuid leaf 15H";
  tsc_tick_count::setDescription(desc.str());
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
  std::stringstream desc;
  desc << "X86 TSC " << formatTimer(*time) << " from " << brandString;
  tsc_tick_count::setDescription(desc.str());

  return true;
}

static double measureTSCtickOnce() {
  // Use C++ "steady_clock" since cppreference.com recommends against
  // using hrtime.  Busy wait for 1ms based on the std::chrono clock
  // and time that with rdtsc.
  // Assuming the steady clock has a reasonable resolution, 1ms should be
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
static double measureTSCtick(char const * warning) {
  double minTick = 1.0; /* We know it must be faster than one per second! */

  for (int i = 0; i < 5; i++) {
    minTick = std::min(minTick, measureTSCtickOnce());
  }

  std::stringstream desc;
  desc << "X86 TSC " << formatTimer(minTick) << " from measurement" << warning;
  tsc_tick_count::setDescription(desc.str());
  return minTick;
}

static double readHWTickTime() {
  // First check whether TSC can sanely be used at all.
  // These leaves are common to Intel and AMD.
  Target::cpuid_t cpuinfo;

  // Does the leaf that can tell us that exist?
  x86_cpuid(0x80000000, 0, &cpuinfo);
  if (cpuinfo.eax >= 0x80000007) {
    // At least the CPU can tell us whether it supports an invariant TSC.
    x86_cpuid(0x80000007, 0, &cpuinfo);
    if ((cpuinfo.edx & (1 << 8))) {
      // The CPU does have invariant TSC, so this should all be OK.
      double res;
      // Try to get it from Intel's leaf15H
      if (extractLeaf15H(&res)) {
        return res;
      }
      // Try to get it from the brand name of the CPU (Intel have it there as a string),
      // and do seem to have the TSC clock run at the notional CPU frequency.
      // Be careful of Apple's emulation, though. It claims to be "GenuineIntel", but
      // the clock frequency it announces in the model name is *not* the rate
      // at which the emulated TSC runs!
      if (Target::onIntel() && !Target::onAppleRosetta()) {
        if (readHWTickTimeFromName(&res)) {
          return res;
        }
      }
      // OK, we have invariant TSC, but we're on AMD or Apple emulation or..., all we can do is measure it.
      return measureTSCtick("");
    }
  }
  // Either it can't tell us, or the CPU doesn't admit to having invariant TSC.
  // In that case, timing using TSC is dubious at best.
  // However... there seem to be some VMs (e.g. on Windows), which do that.
  // So we'll assume that they do, really, have invariant TSC, but are just
  // not admitting it.
  //
  printWarning("timer may be wrong, cpuid did not report an invariant TSC!");
  return measureTSCtick(
      " (***MAY BE WRONG*** cpuid does not state invariant TSC.)");
}
#endif /* Architectures */
#else
/* Target does not have code to support a high-resolution low cost hardware timestamp */
static double readHWTickTime() {
  double tick = Target::readHWTickTime();
  std::stringstream desc;

  desc << "std::chrono::steady_clock " << formatTimer(tick);
  tsc_tick_count::setDescription(desc.str());

  return tick;
}
#endif

std::string tsc_tick_count::timerDescription() {
  // readHWTickTime also sets the description when first executed.
  if (Description == "") {
    (void)readHWTickTime();
  }
  return Description;
}

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
