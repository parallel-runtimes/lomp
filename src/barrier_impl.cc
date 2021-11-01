//===-- barrier_impl.cc - The barrier zoo -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementations of many different barriers.
/// All of the barriers present the interface of lomp::Barrier, and this file exports
/// the single function findBarrier, which allows external code to choose a barrier by name.

// TODO: By default this compiles >26 barriers, for use in the runtime that is likely
// unnecessary, we should just compile in the one or two that we need, to save compile time
// and code space.

#include <omp.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <cstdlib>

// algorithm just for std::max
#include <algorithm>

// Avoid including more lomp runtime interfaces; this code is also used in the
// barriers microbenchmark, or could be used elsewhere, and is self contained.
#include "barriers.h"
#include "target.h"
#include "debug.h"

#if (DEBUG > 0)
#include <sstream>
#endif

namespace lomp {
// Derive publicly from this to force an aligned allocator to be used for the given object.
// C++17 has better ways of doing this, but we're sticking to C++14...
template <int alignment>
class alignedAllocators {
  static void * doAllocation(std::size_t bytes) {
    void * res;
    res = std::aligned_alloc(alignment, bytes);
    if (!res) {
      fatalError("Aligned memory allocation failed.");
    }
    return res;
  }

public:
  static void * operator new[](std::size_t bytes) {
    return doAllocation(bytes);
  }
  static void * operator new(std::size_t bytes) {
    return doAllocation(bytes);
  }
  static void operator delete[](void * space) {
    free(space);
  }
  static void operator delete(void * space) {
    free(space);
  }
};

// Be careful with these. They force the individual item to be cache-aligned and padded to the size of the cacheline.
// If you want a single uint32 aligned at the start of a line with other data following it, DO NOT USE THIS!
class AlignedUint32 : public alignedAllocators<CACHELINE_SIZE> {
  alignas(CACHELINE_SIZE) uint32_t Value;

public:
  AlignedUint32(uint32_t v) : Value(v) {}
  AlignedUint32() : Value(0) {}
  operator uint32_t() const {
    return Value;
  }
  uint32_t operator++(int) {
    return Value++;
  }
};

class AlignedAtomicUint32 : public alignedAllocators<CACHELINE_SIZE> {
  alignas(CACHELINE_SIZE) std::atomic<uint32_t> Value;

public:
  AlignedAtomicUint32(uint32_t v) : Value(v) {}
  AlignedAtomicUint32() : Value(0) {}
  operator uint32_t() const {
    return Value;
  }
  void store(uint32_t v, std::memory_order o = std::memory_order_seq_cst) {
    Value.store(v, o);
  }
  auto load(std::memory_order o = std::memory_order_seq_cst) {
    return Value.load(o);
  }
  uint32_t operator++(int) {
    return Value.fetch_add(1);
  }
};

// A simple broadcast operation in which all worker threads poll the
// same cache-line. We do not expect that this is optimal, but it is,
// relatively, simple.
//
// This trivial implementation does not do anything smart about waiting.
// We need a separate class to encapsulate waiting, but that will come later.
// Similarly it won't execute tasks, which needs to be inside the waiting class.
//
class NaiveBroadcast : public alignedAllocators<CACHELINE_SIZE> {
  // Put the payload and the flag into the same cache line.
  CACHE_ALIGNED std::atomic<uint32_t> Flag;
  InvocationInfo const * OutlinedBody;

  // And the per-thread state into a cacheline/thread.
  AlignedUint32 * const NextValues;

public:
  NaiveBroadcast(int NumThreads) : NextValues(new AlignedUint32[NumThreads]) {
    LOMP_ASSERT((uintptr_t(this) & (CACHELINE_SIZE - 1)) == 0);
    Flag = ~0;
    for (int i = 0; i < NumThreads; i++) {
      // So that threads all look for the value which isn't there yet.
      NextValues[i] = 0;
    }
  }
  ~NaiveBroadcast() {
    delete[] NextValues;
  }

  void wakeUp(int me, InvocationInfo const * Args) {
    OutlinedBody = Args;
    // Ensure that all previous stores are visible before this.
    Flag.store(NextValues[me], std::memory_order_release);
    NextValues[me] = ~NextValues[me];
  }

  InvocationInfo const * wait(int me) {
    uint32_t NextFlag = NextValues[me];

    // Much better polling should be used here!
    while (Flag.load(std::memory_order_acquire) != NextFlag)
      ;
    // Flip the value this thread looks for next time.
    NextValues[me] = ~NextFlag;
    return OutlinedBody;
  }
};

// A broadcast which uses a specific fan-out per cacheline. I'd expect
// to use a power of two here. It also treats the case where the
// fanout is one specially, since there the thread can reset the value
// and there is no need for the alternating approach.
// This could also be cleverer, and avoid the need for two stores in
// the case where it is broadcasting a value, since if we know the
// value will not be some specific value (say uintprt_t(0), or uintptr_t(-1),
// neither of which will reasonably occur for a function pointer), then
// we can use any other value as the flag. We would need to switch
// to two sets of flags and a reset, though, since we couldn't do up/down
// easily.
template <int LBW>
class LBWBroadcast : public alignedAllocators<CACHELINE_SIZE> {
  // Shared data, sitting in a single line, we hope.
  struct flagLine : public alignedAllocators<CACHELINE_SIZE> {
    alignas(CACHELINE_SIZE) std::atomic<uint32_t> Flag;
    InvocationInfo const * OutlinedBody;
  } * goFlags;
  AlignedUint32 * NextValues;
  int NumThreads;

public:
  LBWBroadcast(int nThreads) : NumThreads(nThreads) {
    goFlags = new flagLine[(NumThreads + LBW - 1) / LBW];
    for (int i = 0; i < (NumThreads + LBW - 1) / LBW; i++) {
      goFlags[i].Flag.store(0, std::memory_order_relaxed);
    }
    if (LBW == 1) {
      NextValues = 0;
    }
    else {
      NextValues = new AlignedUint32[NumThreads];
      for (int i = 0; i < NumThreads; i++) {
        NextValues[i] = ~uint32_t(0);
      }
    }
    // No fences needed here since to use this other threads will need
    // to find it, and however that happens must include a fencing operation.
  }
  ~LBWBroadcast() {
    delete[] goFlags;
    if (LBW != 1) {
      delete[] NextValues;
    }
  }
  void wakeUp(int me, InvocationInfo const * args) {
    auto nextValue = (LBW == 1) ? ~uint32_t(0) : uint32_t(NextValues[0]);
    if (args) {
      for (int i = 0; i < (NumThreads + LBW - 1) / LBW; i++) {
        auto flags = &goFlags[i];
        // Copy the value to the cache line wih the flag so that it
        // gets pulled immediately
        flags->OutlinedBody = args;
        flags->Flag.store(nextValue, std::memory_order_release);
      }
    }
    else {
      // Introduce a single store fence here, but then allow the other
      // stores to be relaxed, since they can be unordered wrt each
      // other, and we want lots of stores in flight together.
      std::atomic_thread_fence(std::memory_order_release);
      for (int i = 0; i < (NumThreads + LBW - 1) / LBW; i++) {
        auto flags = &goFlags[i];
        flags->Flag.store(nextValue, std::memory_order_relaxed);
      }
    }
    if (LBW != 1) {
      NextValues[me] = ~nextValue;
    }
  }
  InvocationInfo const * wait(int me) {
    auto expected = (LBW == 1) ? ~uint32_t(0) : uint32_t(NextValues[me]);
    auto myFlags = &goFlags[me / LBW];
    while (myFlags->Flag.load(std::memory_order_acquire) != expected) {
      Target::Yield();
    }
    if (LBW == 1) {
      goFlags[me].Flag.store(0, std::memory_order_relaxed);
    }
    else {
      NextValues[me] = ~expected;
    }
    return myFlags->OutlinedBody;
  }
};

// No need to be clever here. (So no need to use the logarithmic,
// complexity recursive squaring scheme, which is probably more costly
// anyway because of poor branch prediction!)
static inline int power(int base, int n) {
  // Often used with a template parameter as base, so elide some of the obvious cases.
  // If we trusted the compiler we wouldn't bother to do this :-)
  switch (base) {
  case 2:
    return 1 << n;
  case 4:
    return 1 << (2 * n);
  case 8:
    return 1 << (3 * n);
  default:
    break;
  }
  int value = 1;
  for (int i = 0; i < n; i++) {
    value = value * base;
  }
  return value;
}

static int ceilingLogN(int base, int value) {
  if (value == 1) {
    return 0;
  }
  int i = 1;
  int p = base;
  while (p < value) {
    i += 1;
    p = p * base;
  }
  return i;
}

// static inline int ceilingLog2(uint32_t n) { return ceilingLogN(2, n); }

// Different implementations of "counters"

// Implement a counter as an array of bytes, where a thread writes
// 0xFF to its byte when it has arrived.  This version is templated,
// and an appropriately sized instance must be used.  (Where
// "appropriately sized" means that the minimum number of uint64_t
// entries is used.)
template <int maxCount>
class flagCounter {
  typedef union {
    std::atomic<uint64_t> allFlags;
    std::atomic<uint8_t> flags[8];
  } byteWord;
  enum { numEntries = (maxCount + 7) / 8, lastEntry = numEntries - 1 };
  alignas(CACHELINE_SIZE) byteWord data[numEntries];
  // The last word may not need all of the bytes, so we initialize
  // it so that entries which don't exist appear present.
  // That avoids a special case in the polling loop.
  uint64_t lastMask;
  // How many entries do we need?
  int lastNeeded;
#if (DEBUG > 0)
  void dump(char const * msg) const {
    debug(Debug::Barriers, "%s Barrier: numEntries: %d, lastMask: 0x%lx", msg,
          numEntries, lastMask);
    for (int i = 0; i < lastEntry; i++) {
      debug(Debug::Barriers, "  [%d] 0x%lx, ", i, data[i].allFlags.load());
    }
    debug(Debug::Barriers, "  [%d] 0x%lx", lastEntry,
          data[lastEntry].allFlags.load());
  }
#else
  void dump(char const *) const {}
#endif

public:
  flagCounter() {
    init(maxCount);
  }
  flagCounter(int count) {
    static_assert(
        maxCount <= 64,
        "flagCounter code needs enhancing to work on more than one cacheline.");
    init(count);
  }
  ~flagCounter() {}

  void init(int count) {
    lastNeeded = ((count + 7) / 8) - 1;

    // Ensure that a suitable sized instantiation has been used.
    LOMP_ASSERT(lastNeeded < numEntries);

    // Work out the mask we need for the last one so that we can  just compare all of the necessary
    // full words with ~uint64_t(0).
    union {
      uint64_t word;
      uint8_t mask[8];
    } lm;
    lm.word = 0;
    // Fill in the flags we don't have threads for in the final uint64_t
    // so that the polling loop always looks for all
    for (auto i = 7; i > ((count - 1) & 7); --i) {
      lm.mask[i] = uint8_t(0xff);
    }
    lastMask = lm.word;
    reset();
    dump("After init");
  }

  // Reset the state. This is on the LILO path, in the barrier, so wants to be fast,
  // Therefore we explicitly unroll the loop ourself.
  // Alternatively we could have each thread clear its own flag, howeever, that
  // is likely to be slower because of all the cache movement it would require.
  // It may be possible to remove this entirely if we used paired counters and
  // the same up/down trick as we do in the dissemination barrier.
  void reset() {
    // If/when we move to C++17 we should add [[fallthrough]] to these entries.
    // If we allow more than one cache line this code needs to be made into a loop
    // over the lines.
    // g++ warns about out of bounds accesses here, but they cannot happen in reality,
    // since we checked lastNeeded is < numEntries.
    switch (lastNeeded - 1) {
    case 6:
      data[6].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 5:
      data[5].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 4:
      data[4].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 3:
      data[3].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 2:
      data[2].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 1:
      data[1].allFlags.store(0, std::memory_order_relaxed);
      FALLTHROUGH;
    case 0:
      data[0].allFlags.store(0, std::memory_order_relaxed);
    }
    data[lastNeeded].allFlags.store(lastMask, std::memory_order_release);
  }

  // Note that this thread is present.
  bool checkIn(int me) {
    // Set our byte in the array.
    data[me / 8].flags[me % 8].store(uint8_t(0xff), std::memory_order_release);
    return me == 0;
  }

  // Poll until we see all of the values filled in
  void wait() const {
    for (int i = 0; i <= lastNeeded; i++) {
      while (data[i].allFlags.load(std::memory_order_acquire) != ~uint64_t(0)) {
        // Pause here; we know we haven't finished, could/should do
        // better waiting.
        Target::Yield();
      }
    }
  }
};

// A simple atomic counter.
template <int maxValue>
class atomicCounter {
  CACHE_ALIGNED std::atomic<uint32_t> present;
  uint32_t num;

public:
  atomicCounter() {
    init(0);
  }
  atomicCounter(int count) {
    init(count);
  }
  ~atomicCounter() {}
  void init(int count) {
    num = count;
    present = 0;
  }
  void reset() {
    present.store(0, std::memory_order_release);
  }
  auto getNum() const {
    return num;
  }
  bool checkIn(int me) {
    ++present;
    return me == 0;
  }
  bool tryCheckin() {
    for (;;) {
      // Test and test and set
      uint32_t current = present.load(std::memory_order_acquire);
      if (current == num) {
        return true;
      }
      else if (present.compare_exchange_strong(current, current + 1)) {
        return false;
      }
    }
  }
  void wait() {
    while (present.load(std::memory_order_acquire) != num) {
      Target::Yield();
    }
  }
};

class AtomicUpDownCounter {
  CACHE_ALIGNED std::atomic<uint32_t> present;
  uint32_t num;

public:
  AtomicUpDownCounter() {
    init(0);
  }
  AtomicUpDownCounter(int count) {
    init(count);
  }
  ~AtomicUpDownCounter() {}
  void init(int count) {
    num = count;
    present = 0;
  }
  void reset() {
    present.store(0, std::memory_order_release);
  }
  void increment() {
    ++present;
  }
  void decrement() {
    --present;
  }
  void waitAll() {
    while (present.load(std::memory_order_acquire) != num) {
      Target::Yield();
    }
  }
  void waitNone() {
    while (present.load(std::memory_order_acquire) != 0) {
      Target::Yield();
    }
  }
};

// TODO: trya barrier in which threadsa always count up, but keep track of how many barriers
// they have executed, so can work out the next target count they are aiming for.
// (Need to use "after" for the comparison to avoid overflow issues...)

// A barrier in which threads count up or down, polling the appropriate counter.
class AtomicUpDownBarrier : public Barrier,
                            public alignedAllocators<CACHELINE_SIZE> {
  enum { MAX_THREADS = 64 };
  AtomicUpDownCounter counters[2];
  AlignedUint32 barrierCounts[MAX_THREADS];

public:
  AtomicUpDownBarrier(int NumThreads) {
    for (int i = 0; i < NumThreads; i++) {
      barrierCounts[i] = 0;
    }
    counters[0].init(NumThreads);
    counters[1].init(NumThreads);
  }

  static Barrier * newBarrier(int NumThreads) {
    return new AtomicUpDownBarrier(NumThreads);
  }

  ~AtomicUpDownBarrier() {}

  void fullBarrier(int me) {
    uint32_t myCount = barrierCounts[me];
    auto countIdx = myCount & 1;
    auto activeCounter = &counters[countIdx];
    bool countUp = (myCount & 2) == 0;
    if (countUp) {
      activeCounter->increment();
      activeCounter->waitAll();
    }
    else {
      activeCounter->decrement();
      activeCounter->waitNone();
    }
    barrierCounts[me]++;
  }

  bool checkIn(int) {
    fatalError(
        "Cannot use checkIn in an AtomicUpDown, non-centralized, barrier\n");
  }

  void wakeUp(int, InvocationInfo const *) {
    fatalError(
        "Cannot use wakeup in an AtomicUpDown, non-centralized, barrier\n");
  }

  InvocationInfo const * checkOut(bool, int) {
    fatalError(
        "Cannot use wakeup in an AtomicUpDown, non-centralized, barrier\n");
  }
  static char const * barrierName() {
    return "AtomicUpDown";
  }
  char const * name() const {
    return barrierName();
  }
};

// Tree barriers.
// A fixed tree in which the topology of communication is pre-determined.
// We don't bother with a tree in which all threads start at the leaf (a tournament tree),
// since it is one layer deeper and all threads see the full check-in latency.
//
template <int branchingFactor, template <int> class counter>
class fixedTreeCheckIn {
  // Per thread data
  enum { MAX_THREADS = 256 };
  int NumThreads;

  struct {
    CACHE_ALIGNED int parent; // Parent
    int position;             // Position within parent, in case it matters
    int numChildren;          // Number of children I have
    int sequence;             // Barrier count.
  } localData[MAX_THREADS];
  // Shared data
  counter<branchingFactor>
      Counters[2][(MAX_THREADS + branchingFactor - 1) / branchingFactor];

#if (DEBUG > 0)
  void dump(char const * msg) const {
    debug(Debug::Barriers, "%s fixedTreeCheckin<%d>: ", msg, branchingFactor);
    for (int i = 0; i < NumThreads; i++) {
      debug(Debug::Barriers, "  [%d] parent %d(%d), children %d", i,
            localData[i].parent, localData[i].position,
            localData[i].numChildren);
    }
  }
#else
  void dump(char const *) const {}
#endif
public:
  fixedTreeCheckIn(int count) : NumThreads(count) {
    init(count);
  }
  ~fixedTreeCheckIn() {}
  void init(int count) {
    NumThreads = count;
    // Compute the tree.
    // N.B. This does not balance the last layer, which it should.
    for (int me = 0; me < NumThreads; me++) {
      // Clean the seqence numbers.
      localData[me].sequence = 0;

      // Compute the tree.
      localData[me].parent = (me + branchingFactor - 1) / branchingFactor - 1;
      localData[me].position = (me - 1) % branchingFactor;

      // And the number of children
      int numChildren = 0;
      if (branchingFactor * me < NumThreads) {
        if (branchingFactor * (me + 1) >= NumThreads) {
          numChildren = NumThreads - (me * branchingFactor) - 1;
        }
        else {
          numChildren = branchingFactor;
        }
        // Initialize the counters.
        Counters[0][me].init(numChildren);
        Counters[1][me].init(numChildren);
      }
      localData[me].numChildren = numChildren;
    }
    dump("After barrier initialization");
  }

  // No need to explicit reset
  void reset() {}
  bool checkIn(int me) {
    int parity = (localData[me].sequence++) & 1;
    auto counterArray = &Counters[parity][0];
    int children = localData[me].numChildren;
    if (children) {
      Counters[!parity][me].reset();
      // Wait for our children
      counterArray[me].wait();
    }
    if (me != 0) {
      // Pass the message on up to our parent.
      counterArray[localData[me].parent].checkIn(localData[me].position);
    }
    return me == 0;
  }
  void wait() {
    // No need for explicit wait; we wait for what we need to inside the checkin code.
  }
};

template <int branchingFactor>
class dynamicTreeCheckIn {
  // Shared data which is not updated
  int NumThreads;
  int NumSlots;
  int Depth;

  // Per thread data
  enum { LN2_MAX_THREADS = 8, MAX_THREADS = (1 << LN2_MAX_THREADS) };
  struct {
    CACHE_ALIGNED int position
        [LN2_MAX_THREADS]; // Counter to look at at each level up the tree
    int sequence;          // Barrier count.
  } localData[MAX_THREADS];

  enum {
    // If we used an imbalanced allocation at the leaf this shoudl work
    // MAX_NUM_SLOTS = (MAX_THREADS + branchingFactor - 1) / branchingFactor
    // but since we don't, we may need more, and this should be safe. I think!
    MAX_NUM_SLOTS = (MAX_THREADS + 1) / 2
  };
  // Shared data which is updated
  atomicCounter<branchingFactor> Counters[2][MAX_NUM_SLOTS];

#if (DEBUG > 0)
  std::string formatThread(int me) const {
    std::stringstream res;
    res << "path: ";

    for (int i = 0; localData[me].position[i]; i++) {
      res << localData[me].position[i] << ", ";
    }
    // Everyone should end up in the final as the last possibility!
    res << "0";

    return res.str();
  }

  void dump(char const * msg) const {
    debug(Debug::Barriers, "%s dynamicTreeCheckin<%d>: ", msg, branchingFactor);
    for (int i = 0; i < NumThreads; i++) {
      debug(Debug::Barriers, "  Thread[%d] %s", i, formatThread(i).c_str());
    }
    for (int i = 0; i < NumSlots; i++) {
      auto targetCount = Counters[0][i].getNum();
      if (targetCount) {
        debug(Debug::Barriers, "  Slot[%d] %d", i, targetCount);
      }
    }
  }
#else
  void dump(char const *) const {}
#endif
  void setPath(int thread, int * bases) {
    int round = 0;
    auto positions = &localData[thread].position[0];
    for (int d = Depth; d > 1; d--) {
      auto slots = power(branchingFactor, d - 1);
      // Allocate threads cyclically to the slots so that it is balanced
      // TODO: (if we ever really want to use this).
      // This is simple, but possibly a poor way to do things since it
      // does not preserve locality, so in a two socket machine we'll
      // generate more cross socket traffic than should be required...
      // Better would probably be to do a placement like the default
      // static loop schedule.
      // Check if this is the first round, and I have a pass straight
      // into the second round.
      if (d == Depth &&     // First round
          thread < slots && // Not already wrapped
          (thread + slots) >=
              NumThreads) { // There is no other competitor to play against.
        debug(Debug::Barriers + 1,
              "T %d, Depth %d, slots %d => pass this round", thread, d, slots);
        continue;
      }
      auto mypos = thread % slots;
      debug(Debug::Barriers + 1, "T %d, Depth %d, slots %d => mypos %d", thread,
            d, slots, mypos);
      DEBUG_ASSERT(mypos < NumSlots);
      positions[round++] = bases[d - 1] + mypos;
      thread = mypos;
    }
    // Ultimately anyone can play in the final!
    positions[round] = 0;
  }

public:
  dynamicTreeCheckIn(int count) : NumThreads(count) {
    init(count);
  }
  ~dynamicTreeCheckIn() {}
  void init(int count) {
    LOMP_ASSERT(count <= MAX_THREADS);
    NumThreads = count;
    if (NumThreads == 1) {
      return;
    }

    Depth = ceilingLogN(branchingFactor, NumThreads);
    int startBase[LN2_MAX_THREADS];
    startBase[0] = 0;
    auto p = 1;
    for (int d = 1; d < Depth; d++, p = p * branchingFactor) {
      startBase[d] = startBase[d - 1] + p;
      debug(Debug::Barriers + 1, "Depth %d base %d", d, startBase[d]);
    }

    auto firstRoundBase = startBase[Depth - 1];
    NumSlots = firstRoundBase +
               std::min(NumThreads, power(branchingFactor, Depth - 1));
    debug(Debug::Barriers, "Threads %d: firstRoundBase:%d", NumThreads,
          firstRoundBase);

    // Compute the tree; each thread works out its
    // path to the root.
    for (int me = 0; me < NumThreads; me++) {
      setPath(me, &startBase[0]);
      // Clean the seqence numbers.
      localData[me].sequence = 0;
    }
    // Fill in the counts; we want one less than the number at each
    // point so that the final entrant can easily see that everyone
    // else has arrived.  By default the number is the branching
    // ratio-1, so fill that in everywhere to begin with.
    // Above the leaf the tree is dense, so all counters are full.
    for (int i = 0; i < firstRoundBase; i++) {
      Counters[0][i].init(branchingFactor - 1);
      Counters[1][i].init(branchingFactor - 1);
    }

    // First round uses slots >= firstRoundBase.
    // Now fix the counters in the leaf layer which may be less used.
    int counts[MAX_NUM_SLOTS];
    LOMP_ASSERT(NumSlots < MAX_NUM_SLOTS);
    for (int i = 0; i < NumSlots; i++) {
      counts[i] = 0;
    }
    debug(Debug::Barriers, "firstRoundBase = %d", firstRoundBase);
    for (int t = 0; t < NumThreads; t++) {
      auto frs = localData[t].position[0];
      debug(Debug::Barriers, "Thread %d uses leaf slot %d", t, frs);
      if (frs < firstRoundBase) {
        // This thread has a pass in the first round.
        continue;
      }
      debug(Debug::Barriers, " Incrementing count for %d ", frs);
      counts[frs - firstRoundBase]++;
    }
    for (int i = firstRoundBase; i < NumSlots; i++) {
      if (counts[i - firstRoundBase] == 0) {
        break; // We may not use all of the first round slots.
      }
      Counters[0][i].init(counts[i - firstRoundBase] - 1);
      Counters[1][i].init(counts[i - firstRoundBase] - 1);
    }
    dump("After barrier initialization");
  }

  // No need to explicitly reset
  void reset() {}
  bool checkIn(int me) {
    if (UNLIKELY(NumThreads == 1)) {
      return true;
    }

    auto sequence = localData[me].sequence++;
    auto parity = sequence & 1;
    auto counterArray = &Counters[parity][0];

    // No geometric relationship here, but just have each thread own of the
    // counters and reset it so as to distribute the work.
    if (me < NumSlots) {
      debug(Debug::Barriers + 1, "[%d] reset counter[%d][%d]", me, !parity, me);
      Counters[!parity][me].reset();
    }
    auto positions = &localData[me].position[0];
    // The Depth is the maximum possible depth. When threads have a pass in
    // the first round, their depth will be one less.  The final is always on
    // court zero, though, so if we won when playing there we are the
    // champion.
    for (int d = 0;; d++) {
      auto pos = positions[d];
      if (!counterArray[pos].tryCheckin()) {
        debug(Debug::Barriers + 1, "[%d]%d Depth %d, lost", me, sequence, d);
        return false;
      }
      debug(Debug::Barriers + 1, "[%d]%d Depth %d, won", me, sequence, d);
      // Did we just win on court zero?
      if (pos == 0) {
        return true;
      }
    }
    fatalError("Reached unreachable code: " __FILE__ ":" STRINGIFY(__LINE__));
  }
  void wait() {
    // No need for explicit wait; we wait for what we need to inside the checkin code.
  }
};

/* A general, centralized barrier. The checkIn function should return false
 * in all threads other then the one which needs to check whether
 * all threads have arrived. (In a dynamic tree that call is empty anyway).
 * It templates the check in and broadcast aspects so that it is easy
 * to construct barriers with different check-in and check-out styles.
 * 
 * We show fixed tree checkin and two different counter check in schemes.
 * Similarly we can use different checkout schemes (naive or tree broadcasts).
 * Coupled with branching ratios and different ratios of threads sharing/cacheline
 * in the LBW broadcast, we can rapidly produce many many barriers!
 */
template <class counter, class broadcast, char const * fullName>
class centralizedBarrier : public Barrier,
                           public alignedAllocators<CACHELINE_SIZE> {
  counter CheckedIn;
  broadcast Broadcast;

public:
  centralizedBarrier(int NumThreads)
      : CheckedIn(NumThreads), Broadcast(NumThreads) {}

  static Barrier * newBarrier(int NumThreads) {
    return new centralizedBarrier(NumThreads);
  }

  ~centralizedBarrier() {}

  bool checkIn(int me) {
    debug(Debug::Barriers + 1, "%d: Checking in", me);

    if (CheckedIn.checkIn(me)) {
      // I am the master thread, so I may need to wait for everyone to arrive!
      // (In a dynamic tree we don't need to do anything, since the root
      // thread won't complete its checkin until all threads have arrived).
      debug(Debug::Barriers + 1, "%d: root waiting for everyone to get here",
            me);
      CheckedIn.wait();
      // Safe because all the threads are waiting to be released which
      // happens via a different route
      CheckedIn.reset();
      debug(Debug::Barriers + 1, "%d: root, all here", me);
      return true;
    }
    else {
      debug(Debug::Barriers + 1, "%d: checked in, not root", me);
      return false;
    }
  }

  void wakeUp(int me, InvocationInfo const * Args) {
    Broadcast.wakeUp(me, Args);
  }

  InvocationInfo const * checkOut(bool root, int me) {
    if (root) {
      debug(Debug::Barriers + 1, "%d: Waking everyone up.", me);
      wakeUp(me, nullptr);
      return (nullptr);
    }
    else {
      debug(Debug::Barriers + 1, "%d: waiting in checkout.", me);
      auto res = Broadcast.wait(me);
      debug(Debug::Barriers + 1, "%d:   woken.", me);
      return res;
    }
  }
  static char const * barrierName() {
    return fullName;
  }
  char const * name() const {
    return barrierName();
  }
};

static char const AcNbName[] = "Atomic counter; Naive broadcast";
typedef class centralizedBarrier<atomicCounter<0>, NaiveBroadcast, AcNbName>
    AtomicNaiveBarrier;
static char const FcNbName[] = "Flag counter; Naive broadcast";
typedef class centralizedBarrier<flagCounter<64>, NaiveBroadcast, FcNbName>
    FlagNaiveBarrier;

#define LBWNAME(lbw) "LBW " STRINGIFY(lbw) " broadcast"
#define LBWCLASS(lbw) LBWBroadcast<lbw>
#define LBWTAG(lbw) LBW##lbw

#define EXPAND_LBW_BARRIER(checkinClass, checkinName, lbw)                     \
  static char const checkinName##LBW##lbw##BarrierName[] =                     \
      STRINGIFY(checkinName) " counter; " LBWNAME(lbw);                        \
  typedef class centralizedBarrier<checkinClass, LBWBroadcast<lbw>,            \
                                   checkinName##LBW##lbw##BarrierName>         \
      checkinName##LBW##lbw##Barrier;

// clang-format off
#define FOREACH_LBW(macro, checkinClass, checkinName)   \
  macro(checkinClass, checkinName, 1)                   \
  macro(checkinClass, checkinName, 2)                   \
  macro(checkinClass, checkinName, 4)                   \
  macro(checkinClass, checkinName, 8)                   \
  macro(checkinClass, checkinName, 64)
// clang-format on
FOREACH_LBW(EXPAND_LBW_BARRIER, atomicCounter<0>, Atomic)
FOREACH_LBW(EXPAND_LBW_BARRIER, flagCounter<64>, Flag)

// Various trees.
#define EXPAND_FIXEDTREE_BARRIER(branch, checkinClass, checkinName,            \
                                 broadcastClass, broadcastName)                \
  static char const                                                            \
      fixedTree##branch##checkinName##broadcastName##BarrierName[] =           \
          "FixedTree(" STRINGIFY(branch) ")" STRINGIFY(                        \
              checkinName) ";" STRINGIFY(broadcastName) " broadcast";          \
  typedef class centralizedBarrier<                                            \
      fixedTreeCheckIn<branch, checkinClass>, broadcastClass,                  \
      fixedTree##branch##checkinName##broadcastName##BarrierName>              \
      FT##branch##checkinName##broadcastName##Barrier;

// clang-format off
#define FOREACH_TREE_BARRIER(macro, checkinClass, checkinName, broadcastClass, \
                             broadcastName)				\
  macro(2, checkinClass, checkinName, broadcastClass, broadcastName)	\
  macro(4, checkinClass, checkinName, broadcastClass, broadcastName)	\
  macro(8, checkinClass, checkinName, broadcastClass, broadcastName)	\
  macro(16, checkinClass, checkinName, broadcastClass, broadcastName)

#define FOREACH_DYNAMICTREE_BARRIER(macro, broadcastClass, broadcastName) \
  macro(2, broadcastClass, broadcastName)				\
  macro(4, broadcastClass, broadcastName)				\
  macro(8, broadcastClass, broadcastName)				\
  macro(16, broadcastClass, broadcastName)
// clang-format on

FOREACH_TREE_BARRIER(EXPAND_FIXEDTREE_BARRIER, atomicCounter, Atomic,
                     NaiveBroadcast, Naive)
FOREACH_TREE_BARRIER(EXPAND_FIXEDTREE_BARRIER, flagCounter, Flag,
                     NaiveBroadcast, Naive)
FOREACH_TREE_BARRIER(EXPAND_FIXEDTREE_BARRIER, atomicCounter, Atomic,
                     LBWBroadcast<4>, LBW4)
FOREACH_TREE_BARRIER(EXPAND_FIXEDTREE_BARRIER, flagCounter, Flag,
                     LBWBroadcast<4>, LBW4)

#define EXPAND_DYNAMICTREE_BARRIER(branch, broadcastClass, broadcastName)      \
  static char const dynamicTree##branch##broadcastName##BarrierName[] =        \
      "DynamicTree(" STRINGIFY(branch) ");" STRINGIFY(                         \
          broadcastName) " broadcast";                                         \
  typedef class centralizedBarrier<                                            \
      dynamicTreeCheckIn<branch>, broadcastClass,                              \
      dynamicTree##branch##broadcastName##BarrierName>                         \
      DT##branch##broadcastName##Barrier;

static char const Tc2ANbName[] = "FixedTree(2) Atomic; Naive broadcast";
typedef class centralizedBarrier<fixedTreeCheckIn<2, atomicCounter>,
                                 NaiveBroadcast, Tc2ANbName>
    FixedTree2AtomicNaiveBarrier;

FOREACH_DYNAMICTREE_BARRIER(EXPAND_DYNAMICTREE_BARRIER, NaiveBroadcast, Naive)
FOREACH_DYNAMICTREE_BARRIER(EXPAND_DYNAMICTREE_BARRIER, LBWBroadcast<4>, LBW4)

// An all to all barrier. We use an atomic counter on each thread
// We could equally make this into a template and use our other, flag
// counter here, though that would make the barrier quite large.
class AllToAllAtomicBarrier : public Barrier,
                              public alignedAllocators<CACHELINE_SIZE> {
  enum { MAX_THREADS = 64 };
  uint32_t NumThreads;
  AlignedAtomicUint32 flags[2][MAX_THREADS];
  AlignedUint32 sequence[MAX_THREADS];

public:
  AllToAllAtomicBarrier(int NThreads) : NumThreads(NThreads) {
    LOMP_ASSERT(NumThreads <= MAX_THREADS);
    for (uint32_t i = 0; i < NumThreads; i++) {
      flags[0][i].store(0, std::memory_order_relaxed);
      // No need to clean flags[1] here; they'll be cleared
      // by each thread when it checks in.
      sequence[i] = 0;
    }
  }

  static Barrier * newBarrier(int NumThreads) {
    return new AllToAllAtomicBarrier(NumThreads);
  }

  ~AllToAllAtomicBarrier() {}

  bool checkIn(int me) {
    auto odd = sequence[me] & 1;
    // If I am checking in to barrier n, no one can be checking in
    // to barrier n+1 yet, so I can safely clean it here.
    flags[!odd][me].store(0, std::memory_order_relaxed);
    // Tell everyone we're here.
    for (uint32_t i = 0; i < NumThreads; i++) {
      flags[odd][i]++;
    }
    return false;
  }

  void wakeUp(int, InvocationInfo const *) {
    fatalError("Wakeup called on non-centralizing (AllToAllAtomic) barrier");
    // Though, actually, we could do this by storing a central value,
    // then having everyone pull it after they wake up.
    // It doesn't seem worthwhile, though, since this is a horrible barrier,
    // so we are unlikely to use it!
  }

  InvocationInfo const * checkOut(bool, int me) {
    // Pull current sequence and move on to the next.
    auto odd = sequence[me]++ & 1;
    while (flags[odd][me] != NumThreads) {
      Target::Yield();
    }
    return 0;
  }
  static char const * barrierName() {
    return "AllToAllAtomic";
  }
  char const * name() const {
    return barrierName();
  }
};

// A general distributed log radix distributed barrier, which can
// be used to build dissemination or hypercube barriers.
// Or, probably others too.
template <int radix>
class distributedLogBarrier : public Barrier,
                              public alignedAllocators<CACHELINE_SIZE> {

  // We want the derived class to be able to see into here since it
  // needs access to things like the number of threads and rounds.
protected:
  // Higher radices will need fewer entries, so this should be safe.
  enum { LN2_MAX_THREADS = 8, MAX_THREADS = (1 << LN2_MAX_THREADS) };
  // Data shared by all threads.
  int NumThreads;
  int NumRounds;

  // Data acessed between threads, but each thread wants a contiguous chunk.
  typedef union {
    CACHE_ALIGNED int forceAlignment;
    std::atomic<bool> Flags[LN2_MAX_THREADS];
  } FlagArray;
  FlagArray ThreadFlags[2][MAX_THREADS];

  struct {
    // Which set of flags should I use next?
    AlignedUint32 EntryCount;
    int Neighbours[LN2_MAX_THREADS];
  } ThreadData[MAX_THREADS];

  // To whom do I need to send in this round?
  virtual int neighbour(int me, int round) const = 0;

  // Cannot call a pure virtual method in the constructor, but the
  // derived classes can call this in their constructor, since by then
  // the base class is fully constructed.
  void computeCommunication() {
    for (int me = 0; me < NumThreads; me++) {
      for (int r = 0; r < NumRounds; r++) {
        auto n = neighbour(me, r);
        ThreadData[me].Neighbours[r] = n;
        debug(Debug::Barriers, "%d -> %d in round %d", me, n, r);
      }
    }
  }

public:
  distributedLogBarrier(int nThreads) : NumThreads(nThreads) {
    NumRounds = ceilingLogN(radix, NumThreads);
    debug(Debug::Barriers, "distributedLogBarrier<%d> %d: %d rounds", radix,
          NumThreads, NumRounds);
    // Clean the data before we start.
    for (int i = 0; i < NumThreads; i++) {
      for (int r = 0; r < NumRounds; r++) {
        ThreadFlags[0][i].Flags[r].store(false, std::memory_order_relaxed);
        ThreadFlags[1][i].Flags[r].store(false, std::memory_order_relaxed);
      }
      ThreadData[i].EntryCount = 0;
    }
  }
  ~distributedLogBarrier() {}
  virtual void fullBarrier(int me) {
    auto parity = ThreadData[me].EntryCount & 1;
    auto sense = (ThreadData[me].EntryCount & 2) == 0;
    auto activeArray = &ThreadFlags[parity][me].Flags[0];

    for (int round = 0; round < NumRounds; round++) {
      auto n = ThreadData[me].Neighbours[round];
      // Tell the appropriate other thread we're here.
      ThreadFlags[parity][n].Flags[round].store(sense,
                                                std::memory_order_release);
      // Then wait for whoever should be talking to us to do so.
      while (activeArray[round].load(std::memory_order_acquire) != sense) {
        Target::Yield();
      }
    }
    // Could mask with 3, but there's no need; we'll never look at higher bits anyway.
    ThreadData[me].EntryCount++;
  }

  // This is a distributed, non-centralizing, barrier.
  bool isDistributed() const {
    return true;
  }

  void wakeUp(int, const InvocationInfo *) {
    fatalError("%s::wakeUp called, but it's a single phase "
               "barrier...", name());
  }
  bool checkIn(int) {
    fatalError("%s::checkIn called, but it's a single phase "
               "barrier...",name());
  }
  InvocationInfo const * checkOut(bool, int) {
    fatalError(
        "%s:checkOut called, but it's a single phase "
        "barrier...",name());
  }
};

class DisseminationBarrier : public distributedLogBarrier<2> {

  virtual int neighbour(int me, int round) const {
    return (me + (1 << round)) % NumThreads;
  }

public:
  DisseminationBarrier(int numThreads) : distributedLogBarrier(numThreads) {
    // Now add the commnication pattern which calls back to our "neighbour"
    // method.
    computeCommunication();
  }
  // Common boilerplate functions.
  static Barrier * newBarrier(int NumThreads) {
    return new DisseminationBarrier(NumThreads);
  }
  static char const * barrierName() {
    return "Dissemination";
  }
  char const * name() const {
    return barrierName();
  }
};

#if (ENABLE_BROKEN_BARRIERS)
#warning ***Beware: This hypercube barrier is broken.***
// This version is fine for powers of 2, but it cannot handle
// non-powers of 2. Consider the simple case of three threads
// 0,1,2:
// in phase 1, 0<->1 and 2 does nothing,
// in phase 2, 0<->2 and 1 does nothing.
// Therefore thread 1 never receives information about thread 2
// and can leave before thread 2 arrives, so this isn't a barrier!
class HypercubeBarrier : public distributedLogBarrier<2> {
  // With whom do I need to communicate in this round?
  int neighbour(int me, int round) const {
    return me ^ (1 << round);
  }

public:
  HypercubeBarrier(int nThreads) : distributedLogBarrier(nThreads) {
    computeCommunication();
  }
  ~HypercubeBarrier() {}

  // Common boilerplate functions.
  static Barrier * newBarrier(int NumThreads) {
    return new HypercubeBarrier(NumThreads);
  }
  static char const * barrierName() {
    return "Hypercube";
  }
  char const * name() const {
    return barrierName();
  }
};
#define expandHyperCube(macro) macro(Hypercube)
#else
#define expandHyperCube(macro)
#endif

// Clang formatting here mashes them all onto one line,
// which makes it hard to see the structure.
// clang-format off
#define FOREACH_BARRIER(macro)                  \
  macro(AtomicNaive)   macro(FlagNaive)         \
  macro(AtomicLBW1)   macro(FlagLBW1)           \
  macro(AtomicLBW2)   macro(FlagLBW2)           \
  macro(AtomicLBW4)   macro(FlagLBW4)           \
  macro(AtomicLBW8)   macro(FlagLBW8)           \
  macro(AtomicLBW64)  macro(FlagLBW64)          \
  macro(AllToAllAtomic)                         \
  macro(AtomicUpDown)                           \
  macro(Dissemination)                          \
  macro(FT2AtomicNaive)                         \
  macro(FT4AtomicNaive)                         \
  macro(FT8AtomicNaive)                         \
  macro(FT16AtomicNaive)                        \
  macro(FT2FlagNaive)                           \
  macro(FT4FlagNaive)                           \
  macro(FT8FlagNaive)                           \
  macro(FT16FlagNaive)                          \
  macro(FT2AtomicLBW4)                          \
  macro(FT4AtomicLBW4)                          \
  macro(FT8AtomicLBW4)                          \
  macro(FT16AtomicLBW4)                         \
  macro(FT2FlagLBW4)                            \
  macro(FT4FlagLBW4)                            \
  macro(FT8FlagLBW4)                            \
  macro(FT16FlagLBW4)				\
  macro(DT2Naive)                               \
  macro(DT4Naive)                               \
  macro(DT8Naive)                               \
  macro(DT16Naive)				\
  macro(DT2LBW4)                                \
  macro(DT4LBW4)                                \
  macro(DT8LBW4)                                \
  macro(DT16LBW4)				\
  expandHyperCube(macro)
  
#define ExpandEntry(name)                               \
  {STRINGIFY(name), lomp::name##Barrier::newBarrier,    \
   lomp::name##Barrier::barrierName},
// clang-format on

static Barrier::barrierDescription AvailableBarriers[] = {
    FOREACH_BARRIER(ExpandEntry)};
enum : int {
  NUM_BARRIERS = sizeof(AvailableBarriers) / sizeof(AvailableBarriers[0]),
};

void Barrier::printBarriers() {
  lomp::errPrintf("Available barriers are : ");
  for (int i = 0; i < NUM_BARRIERS - 1; i++) {
    lomp::errPrintf("'%s',%c", AvailableBarriers[i].name,
                    (((i % 8) == 7) ? '\n' : ' '));
  }
  lomp::errPrintf("'%s'\n", AvailableBarriers[NUM_BARRIERS - 1].name);
}

Barrier::barrierDescription * Barrier::findBarrier(std::string const & wanted) {
  // Linear search is fine and simple. We only do this once
  // at startup.
  for (int i = 0; i < NUM_BARRIERS; i++) {
    if (wanted == AvailableBarriers[i].name) {
      return &AvailableBarriers[i];
    }
  }
  return nullptr;
}

Barrier::barrierDescription * Barrier::getBarrier(int n) {
  if (n < 0 || n >= NUM_BARRIERS) {
    return 0;
  }
  else {
    return &AvailableBarriers[n];
  }
}
} // namespace lomp
