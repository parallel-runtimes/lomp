//===-- loops.h - Implementation of loop scheduling -------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains interfaces to the loop scheduling code.
/// In particular it has the data types needd to handle dynamic loop
/// scheduling, which must be maintained for the life of the loop.
///
//===----------------------------------------------------------------------===//
#ifndef LOOPS_H_INCLUDED
#define LOOPS_H_INCLUDED
#include "target.h"
#include "debug.h"

namespace lomp {
// If we are ony supporting LLVM, it describes all loops for scheduling in
// the simple, canonical form
// for (i = 0; i<=end; i++)
// Therefore, if we are prepared to have a runtime which will not work with
// the (pre-LLVM based) Intel compiler, we can hardwire some values as constants
// allowing the compiler to constant fold out unnecessary operations (such as multiply
// or divide by one).

// The maximum number of concurrently executing parallel loops, (there
// can be more than one because of nowait) This affects the amount of
// space required by the thread descriptor, since each thread needs
// this many loop descriptors (at least for the nonmonotonic:dynamic
// case where each thread has its own state which is also being looked
// at by other threads. Must be a power of 2...
enum { Max_Concurrent_Loops = 16 };
// Setting this to one is useful to check that the loop data is properly
// re-initialized if you;re debugging the library.
// enum { Max_Concurrent_Loops = 1 };

// The information we need to schedule a loop, based on its reduction to canonical form
// for (<loopVarType> i = 0; i<iterationCount; i++)
template <typename loopVarType>
class canonicalLoop {
  typedef typename typeTraits_t<loopVarType>::unsigned_t unsignedType;
#if (ICC_SUPPORTED)
  // Need to have these as variables. For LLVM only support they can be constants.
  loopVarType base; /* Initial value */
  loopVarType incr;
#else
  // LLVM always passes loops in canonical form, so these values can be
  // constant allowing the compiler to fold away expressions involving
  // them. (Particularly divide by one, or subtract zero).
  enum : loopVarType { base = 0, incr = 1 };
#endif
  loopVarType end;    /* Initial value */
  loopVarType scale;  /* Scale factor */
  unsignedType count; /* Number of iterations */

public:
  // Initial form is
  // for (i = b; i<=end; i+= incr) where incr > 0
  // for (i = b; i>=end; i+= incr) where incr < 0
  canonicalLoop(loopVarType b, loopVarType e, loopVarType i, uint32_t chunk) {
    init(b, e, i, chunk);
  }
#if (ICC_SUPPORTED)
  // Initial form is
  // for (i = b; i<=end; i+= incr) where incr > 0
  // for (i = b; i>=end; i+= incr) where incr < 0
  void init(loopVarType b, loopVarType e, loopVarType i, uint32_t chunk) {
    base = b;
    end = e;
    incr = i;
    if (incr > 0) {
      count = 1 + (end - base) / incr;
    }
    else {
      count = 1 + (base - end) / -incr;
    }
    // Scale down by the chunk size.
    count = (count + chunk - 1) / chunk;
    scale = chunk * incr;
  }
#else
  // LLVM passes us the canonical form anyway, so we can assume some of
  // the values and the compiler can fold out divide by 1 and so on.
  void init(loopVarType, loopVarType e, loopVarType, uint32_t chunk) {
    end = e;
    count = end + 1;
    // Scale down by the chunk size.
    count = (count + chunk - 1) / chunk;
    scale = chunk;
  }
#endif
  auto getCount() const {
    return count;
  }
  loopVarType getChunk() const {
    return scale / incr;
  }

  // Return user-space versions of the bounds of a chunk.
  bool isLastChunk(loopVarType iteration) const {
    return unsignedType(iteration) == (count - 1);
  }
  loopVarType getChunkLower(loopVarType iteration) const {
    return base + iteration * scale;
  }
  loopVarType getChunkUpper(loopVarType iteration) const {
    auto chunkEnd = getChunkLower(iteration) + scale - 1;
    debug(Debug(Debug::Loops + 5), "getChunkUpper(%d) initial chunkEnd = %d",
          iteration, chunkEnd);
    if (UNLIKELY(isLastChunk(iteration))) {
      chunkEnd = end;
    }
    debug(Debug(Debug::Loops + 5),
          "getChunkUpper(%d) after adjustment returns %d", iteration, chunkEnd);
    return chunkEnd;
  }
  loopVarType getStride(loopVarType base, loopVarType end) const {
    auto res = getChunkUpper(end) - getChunkLower(base) + 1;
    debug(Debug(Debug::Loops + 5), "getChunkStride(%d,%d) => %u", base, end,
          res);
    return res;
  }
  bool forStaticInit(int32_t schedtype, int32_t * plastIter,
                     loopVarType * plower, loopVarType * pupper,
                     loopVarType * pstride);
};

// For handling a ""static steal" nonmonotonic:dynamic loop schedule.
template <typename unsignedType>
class contiguousWork {
  typedef typename typeTraits_t<unsignedType>::pair_t pairType;

  // N.B. The bounds here are based on a "less than" calculation, unlike the canonical
  // bounds which use <=. Therefore here (0,0) represents no iterations, whereas in the
  // canonical form it represents one iteration (with value zero).
  // Less than is more convenient here, since the empty (0,0) case can occur, and it
  // is hard to represent in the canonical form.
  // N.B. The bounds here are based on a "less than" calculation, unlike the canonical
  // bounds which use <=. Therefore here (0,0) represents no iterations, whereas in the
  // canonical form it represents one iteration (with value zero).
  // Less than is more convenient here, since the empty (0,0) case can occur, and it
  // is hard to represent in the canonical form.
  union CACHE_ALIGNED {
    // For some reason GCC requires that we name the struct, while LLVM is happy
    // for it to be anonymous, so we name it, and then have to type a little more
    // in a few places.
    struct {
      std::atomic<unsignedType> atomicBase;
      std::atomic<unsignedType> atomicEnd;
    } ab;
    pairType pair;
    std::atomic<pairType> atomicPair;
  };
  // Information about the thread state which can be sen by other threads.
  // If it is stealing there is no work here,
  // so no need to look at the other values which may be invalid.
  std::atomic<bool> stealing;
  // Number of iterations this thread has started to execute. (One may still be in flight).
  std::atomic<unsignedType> iterationsStarted;

  auto setBase(unsignedType b, std::memory_order order) {
    return ab.atomicBase.store(b, order);
  }
  auto setEnd(unsignedType e, std::memory_order order) {
    return ab.atomicEnd.store(e, order);
  }

public:
  contiguousWork() {}
  contiguousWork(unsignedType b, unsignedType e)
      : stealing(false), iterationsStarted(0) {
    assign(b, e);
  }
  contiguousWork(contiguousWork * other) {
    // N.B. NOT loaded atomically over both parts, but that's fine, since when we update
    // we'll use a wide CAS, so if it changed at all we'll see it.
    assign(other->getBase(), other->getEnd());
  }
  ~contiguousWork() {}

  auto getBase(std::memory_order order = std::memory_order_acquire) const {
    return ab.atomicBase.load(order);
  }
  auto getEnd(std::memory_order order = std::memory_order_acquire) const {
    return ab.atomicEnd.load(order);
  }
  auto getIterations() const {
    return getEnd() - getBase();
  }
  void initializeBalanced(unsignedType count, uint32_t thread,
                          uint32_t numThreads);
  void assign(unsignedType b, unsignedType e) {
    // No need for atomicity here; we're copying into a local value.
    ab.atomicBase.store(b, std::memory_order_relaxed);
    ab.atomicEnd.store(e, std::memory_order_relaxed);
  }
  void zeroStarted() {
    iterationsStarted.store(0, std::memory_order_release);
  }
  bool trySteal(unsignedType * basep, unsignedType * endp);
  bool incrementBase(unsignedType * oldp);
  auto isStealing() const {
    return stealing.load(std::memory_order_acquire);
  }
  void setStealing() {
    stealing.store(true, std::memory_order_release);
  }
  void clearStealing() {
    stealing.store(false, std::memory_order_release);
  }
  auto getStarted() const {
    return iterationsStarted.load(std::memory_order_acquire);
  }
  // Only the owning thread modifies the started field so this need not be atomic.
  void incrStarted() {
    iterationsStarted.store(getStarted() + 1, std::memory_order_release);
  }
};

// The packed, non-template, version so that we can put one into each thread.
class packedContiguousWork {
  union {
    contiguousWork<uint32_t> work32;
#if LOMP_HAVE_INT128T
    contiguousWork<uint64_t> work64;
#endif
  };

public:
  packedContiguousWork() {}
  ~packedContiguousWork() {}
  auto getWork(uint32_t) {
    return &work32;
  }
#if LOMP_HAVE_INT128T
  auto getWork(uint64_t) {
    return &work64;
  }
#endif
};

// We only need a pointer here, so this is sufficient.
class Thread;

// A single instance of a dynamic loop. *NOT* a template because we want to have
// a single array of these stored in the team structure; hence the union trickery.
class dynamicLoop {
  CACHE_ALIGNED std::atomic<uint32_t> RefCount; // Threads which need
  // to see this descriptor (some may not have arrived yet).
  // The sequence number of this loop: -1 means free.
  std::atomic<int32_t> Sequence;
  // Echoes that in the team, but may be faster to get it from here.
  uint32_t threadCount;
  int32_t schedule;

  // Also need specific data depending on the scheduling option.

  // Iteration space information. Only one is in use at any time.
  // We need one for each of the loop types.
  union {
    canonicalLoop<int32_t> LD32;
    canonicalLoop<uint32_t> LDU32;
    canonicalLoop<int64_t> LD64;
    canonicalLoop<uint64_t> LDU64;
  };

  // The function pointer for this loop which is used to dispatch the
  // next set of iterations for dynamically code-generated
  // loops. (Either real dynamic loops, or schedule(runtime) loops
  // which look dynamic to the compiler even if they're not at
  // runtime).
  template <typename loopVarType>
  using dispatchFunctionPtr = int32_t (dynamicLoop::*)(Thread *, int32_t *,
                                                       loopVarType *,
                                                       loopVarType *,
                                                       loopVarType *);
  // All of this trickery is just to satisfy C++ that we're doing things in a type safe manner.
  // (Even though as soon as there's a union we could store one type and load another).
  union {
    dispatchFunctionPtr<int32_t> dispatch32;
    dispatchFunctionPtr<uint32_t> dispatchU32;
    dispatchFunctionPtr<int64_t> dispatch64;
    dispatchFunctionPtr<uint64_t> dispatchU64;
  };

  // Slight cheat. We just use the widest type for everything.
  std::atomic<uint64_t> nextIteration; /* For monotonic:dynamic and guided. */
  /* For stealing so that one thread can flag everyone when it
   * discovers there's no work  anywhere.
  */
  std::atomic<bool> finished;
  auto isFinished() const {
    return finished.load(std::memory_order_acquire);
  }
  auto setFinished() {
    return finished.store(true, std::memory_order_release);
  }

  void setDispatchFunction(dispatchFunctionPtr<int32_t> d) {
    dispatch32 = d;
  }
  void setDispatchFunction(dispatchFunctionPtr<uint32_t> d) {
    dispatchU32 = d;
  }
  void setDispatchFunction(dispatchFunctionPtr<int64_t> d) {
    dispatch64 = d;
  }
  void setDispatchFunction(dispatchFunctionPtr<uint64_t> d) {
    dispatchU64 = d;
  }

  // The functions used to dispatch iterations. These are the member functions
  // stored in the approptiately typed dispatchFunctionPtr.
  template <typename loopVarType>
  int32_t dispatchStatic(Thread * myThread, int32_t * p_last,
                         loopVarType * p_lb, loopVarType * p_ub,
                         loopVarType * p_st);
  template <typename loopVarType>
  int32_t dispatchStaticChunked(Thread * myThread, int32_t * p_last,
                                loopVarType * p_lb, loopVarType * p_ub,
                                loopVarType * p_st);
  template <typename loopVarType>
  int32_t dispatchGuided(Thread * myThread, int32_t * p_last,
                         loopVarType * p_lb, loopVarType * p_ub,
                         loopVarType * p_st);
  template <typename loopVarType>
  int32_t dispatchMonotonic(Thread * myThread, int32_t * p_last,
                            loopVarType * p_lb, loopVarType * p_ub,
                            loopVarType * p_st);
  template <typename loopVarType>
  int32_t dispatchNonmonotonic(Thread * myThread, int32_t * p_last,
                               loopVarType * p_lb, loopVarType * p_ub,
                               loopVarType * p_st);

public:
  dynamicLoop()
      : RefCount(0), Sequence(-1), threadCount(0), nextIteration(0),
        finished(false) {}
  auto getThreadCount() const {
    return threadCount;
  }
  void setThreadCount(uint32_t n) {
    threadCount = n;
  }

  // Needs to be a template function since it also assigns teh relevant function pointer.
  template <typename loopVarType>
  void setSchedule(int32_t sched);
  auto getSchedule() const {
    return schedule;
  }
  auto getLoopIdx() const {
    return Sequence.load(std::memory_order_relaxed) &
           (Max_Concurrent_Loops - 1);
  }
  bool isUninitialized(int32_t seq) const {
    return (Sequence.load(std::memory_order_acquire) != seq) || (RefCount == 0);
  }
  bool claim(int32_t seq) {
    // Try to claim the loop descriptor, but fail if it is still in use.
    int32_t expected = -1;
    return Sequence.compare_exchange_strong(expected, seq);
  };
  void decrementUse() {
    // Leave a dynamic loop; we might do this before all other threads have even entered it,
    // but RefCount won't get to zero until the last thread has left. (It is initialized
    // to the team size by the thread which does the initialization, so counts threads
    // which have to leave, not those which are actively in the loop at the momoent).
    if (--RefCount == 0) {
      // All have left, mark the descriptor as free for reuse.
      Sequence = -1;
      debug(Debug(Debug::Loops + 5), "dynamicloop released");
    }
  }
  void completeInitialization(uint32_t numThreads) {
    //
    // Allow other threads to see the initialized data and start into the loop.
    //
    finished.store(false, std::memory_order_relaxed);
    RefCount.store(numThreads, std::memory_order_release);
  }
  auto getNextIterationPtr() {
    return &nextIteration;
  }
  // Use of template trickery here to avoid it on the whole class...
  template <typename loopVarType>
  auto getCanonicalLoop();
  template <typename loopVarType>
  dispatchFunctionPtr<loopVarType> getDispatchFunction();
};

template <>
inline auto dynamicLoop::getCanonicalLoop<int32_t>() {
  return &LD32;
}
template <>
inline auto dynamicLoop::getCanonicalLoop<uint32_t>() {
  return &LDU32;
}
template <>
inline auto dynamicLoop::getCanonicalLoop<int64_t>() {
  return &LD64;
}
template <>
inline auto dynamicLoop::getCanonicalLoop<uint64_t>() {
  return &LDU64;
}

template <>
inline dynamicLoop::dispatchFunctionPtr<int32_t>
dynamicLoop::getDispatchFunction<int32_t>() {
  return dispatch32;
}
template <>
inline dynamicLoop::dispatchFunctionPtr<uint32_t>
dynamicLoop::getDispatchFunction<uint32_t>() {
  return dispatchU32;
}
template <>
inline dynamicLoop::dispatchFunctionPtr<int64_t>
dynamicLoop::getDispatchFunction<int64_t>() {
  return dispatch64;
}
template <>
inline dynamicLoop::dispatchFunctionPtr<uint64_t>
dynamicLoop::getDispatchFunction<uint64_t>() {
  return dispatchU64;
}
// Internal interfaces
void initializeLoops();
void getScheduleInfo(omp_sched_t * schedp, int * chunkp);
void setScheduleInfo(omp_sched_t sched, int chunk);
} // namespace lomp
#endif
