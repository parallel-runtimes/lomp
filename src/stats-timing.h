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

#ifndef STATS_TIMING_H
#define STATS_TIMING_H

// Code slightly modified from kmp_stats_timing.h in the LLVM OpenMP runtime.
// Since the licenses are identical this should be fine.
#include <limits>
#include <cstdint>
#include <cmath>
#include <string>
#include "target.h"
#include "debug.h"

namespace lomp {
/* Statistics functions from kmp_statistics.h */
/*
 * A logarithmic histogram. It accumulates the number of values in each power of
 * ten bin.  So 1<=x<10, 10<=x<100, ...
 * Mostly useful where we have some big outliers and want to see information
 * about them.
 */
class logHistogram {
  enum {
    numBins = 31, /* Number of powers of 10. If this changes you need to change
                   * the initializer for binMax */

    /*
     * If you want to use this to analyse values that may be less than 1, (for
     * instance times in s), then the logOffset gives you negative powers.
     * In our case here, we're just looking at times in ticks, or counts, so we
     * can never see values with magnitude < 1 (other than zero), so we can set
     * it to 0.  As above change the initializer if you change this.
     */
    logOffset = 0
  };
  uint32_t zeroCount;
  struct {
    uint32_t count;
    double total;
  } bins[numBins];

  static double binMax[numBins];

#ifdef DEBUG
  uint64_t _total;

  void check() const {
    uint64_t t = zeroCount;
    for (int i = 0; i < numBins; i++)
      t += bins[i].count;
    DEBUG_ASSERT(t == _total);
  }
#else
  void check() const {}
#endif

public:
  logHistogram() {
    reset();
  }

  logHistogram(logHistogram const & o) {
    *this = o;
  }
  logHistogram & operator=(logHistogram const & o) {
    for (int i = 0; i < numBins; i++)
      bins[i] = o.bins[i];
#ifdef DEBUG
    _total = o._total;
#endif
    return *this;
  }

  void reset() {
    zeroCount = 0;
    for (int i = 0; i < numBins; i++) {
      bins[i].count = 0;
      bins[i].total = 0;
    }

#ifdef DEBUG
    _total = 0;
#endif
  }
  uint32_t count(int b) const {
    return bins[b + logOffset].count;
  }
  double total(int b) const {
    return bins[b + logOffset].total;
  }
  static uint32_t findBin(double sample);

  logHistogram & operator+=(logHistogram const & o) {
    zeroCount += o.zeroCount;
    for (int i = 0; i < numBins; i++) {
      bins[i].count += o.bins[i].count;
      bins[i].total += o.bins[i].total;
    }
#ifdef DEBUG
    _total += o._total;
    check();
#endif

    return *this;
  }

  void addSample(double sample);
  int minBin() const;
  int maxBin() const;

  std::string format(char) const;
};

class statistic {
  double minVal CACHE_ALIGNED;
  double maxVal;
  double meanVal;
  double m2;
  uint64_t sampleCount;
  double offset;
  bool collectingHist;
  logHistogram hist;

public:
  statistic(bool doHist = false) {
    reset();
    collectingHist = doHist;
  }
  statistic(statistic const & o) {
    *this = o;
  }
  statistic(double minv, double maxv, double meanv, uint64_t sc, double sd)
      : minVal(minv), maxVal(maxv), meanVal(meanv), m2(sd * sd * sc),
        sampleCount(sc), offset(0.0), collectingHist(false) {}

  statistic & operator=(statistic const & o) {
    minVal = o.minVal;
    maxVal = o.maxVal;
    meanVal = o.meanVal;
    m2 = o.m2;
    sampleCount = o.sampleCount;
    offset = o.offset;
    collectingHist = o.collectingHist;
    hist = o.hist;
    return *this;
  }

  bool haveHist() const {
    return collectingHist;
  }
  void collectHist() {
    collectingHist = true;
  }
  double getMin() const {
    return minVal;
  }
  double getMean() const {
    return meanVal;
  }
  double getMax() const {
    return maxVal;
  }
  uint64_t getCount() const {
    return sampleCount;
  }
  double getSD() const {
    return std::sqrt(m2 / sampleCount);
  }
  double getTotal() const {
    return sampleCount * meanVal;
  }
  logHistogram const * getHist() const {
    return &hist;
  }
  void setOffset(double d) {
    offset = d;
  }

  void reset() {
    minVal = std::numeric_limits<double>::max();
    maxVal = -minVal;
    meanVal = 0.0;
    m2 = 0.0;
    sampleCount = 0;
    offset = 0.0;
    hist.reset();
  }
  void addSample(double sample);
  void scale(double factor);
  void scaleDown(double f) {
    scale(1. / f);
  }
  void forceCount(uint64_t count) {
    sampleCount = count;
  }
  statistic & operator+=(statistic const & other);

  std::string format(char unit, bool total = false) const;
  std::string formatHist(char unit) const {
    return hist.format(unit);
  }
};

/* Times (in clock ticks, whatever those may be!) */
class tsc_tick_count {
private:
  int64_t my_count;
  static double TickTime;
  static std::string Description;

public:
  class tsc_interval_t {
    int64_t value;
    explicit tsc_interval_t(int64_t _value) : value(_value) {}

  public:
    tsc_interval_t() : value(0) {} // Construct 0 time duration
    double seconds() const; // Return the length of a time interval in seconds
    double ticks() const {
      return double(value);
    }
    int64_t getValue() const {
      return value;
    }
    tsc_interval_t & operator=(int64_t nvalue) {
      value = nvalue;
      return *this;
    }

    friend class tsc_tick_count;

    friend tsc_interval_t operator-(const tsc_tick_count & t1,
                                    const tsc_tick_count & t0);
    friend tsc_interval_t operator-(const tsc_tick_count::tsc_interval_t & i1,
                                    const tsc_tick_count::tsc_interval_t & i0);
    friend tsc_interval_t &
    operator+=(tsc_tick_count::tsc_interval_t & i1,
               const tsc_tick_count::tsc_interval_t & i0);
  };

  tsc_tick_count() : my_count(static_cast<int64_t>(Target::readCycleCount())) {}
  tsc_tick_count(int64_t value) : my_count(value) {}
  int64_t getValue() const {
    return my_count;
  }
  static double getTickTime();
  static std::string timerDescription();
  static void setDescription(std::string const & desc) {
    Description = desc;
  }

  // Subtracting and checking the sign of the result handles wrapping,
  // whereas just comparing < or > doesn't.
  bool after(tsc_tick_count const other) const {
    return (my_count - other.my_count) > 0;
  }
  tsc_tick_count later(tsc_tick_count const other) const {
    return after(other) ? (*this) : other;
  }
  bool before(tsc_tick_count const other) const {
    return (my_count - other.my_count) < 0;
  }
  tsc_tick_count earlier(tsc_tick_count const other) const {
    return before(other) ? (*this) : other;
  }

  static tsc_tick_count now() {
    return tsc_tick_count();
  } // returns the current time, since that is what the default constructor uses.
  friend tsc_tick_count::tsc_interval_t operator-(const tsc_tick_count & t1,
                                                  const tsc_tick_count & t0);
};

inline tsc_tick_count::tsc_interval_t operator-(const tsc_tick_count & t1,
                                                const tsc_tick_count & t0) {
  return tsc_tick_count::tsc_interval_t(t1.my_count - t0.my_count);
}

inline tsc_tick_count::tsc_interval_t
operator-(const tsc_tick_count::tsc_interval_t & i1,
          const tsc_tick_count::tsc_interval_t & i0) {
  return tsc_tick_count::tsc_interval_t(i1.value - i0.value);
}

inline tsc_tick_count::tsc_interval_t &
operator+=(tsc_tick_count::tsc_interval_t & i1,
           const tsc_tick_count::tsc_interval_t & i0) {
  i1.value += i0.value;
  return i1;
}

extern std::string formatSI(double interval, int width, char unit);

inline std::string formatSeconds(double interval, int width) {
  return formatSI(interval, width, 'S');
}

inline std::string formatTicks(double interval, int width) {
  return formatSI(interval, width, 'T');
}

class BlockTimer {
  tsc_tick_count Start;
  statistic * Stat;

public:
  BlockTimer(statistic * s) : Stat(s) {}
  ~BlockTimer() {
    Stat->addSample((tsc_tick_count::now() - Start).ticks());
  }
};

#define TIME_BLOCK(s) lomp::BlockTimer __bt__((s))
} // namespace lomp

#endif // STATS_TIMING_H
