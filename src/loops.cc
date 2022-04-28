//===-- loops.cc - Implementation of loop scheduling ------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of OpenMP loop scheduling.
///
//===----------------------------------------------------------------------===//
#include "threads.h"
#include "tasking.h" /* Needed to include interface.h; it probably shouldn't be! */
#include "interface.h"
#include "environment.h"
#include "debug.h"
#include "loops.h"

namespace lomp {

// Initialize internal state to represent the OMP_SCHEDULE envirable.
static struct {
  char const * name;
  kmp_sched_t internalValue;
  omp_sched_t externalValue;
} schedules[] = {
    {"static", kmp_sch_static, omp_sched_static},
    // This is intentional; we want to default to kmp_sch_static on input unless there is a chunk
    // but translate static chunked back to static on output.
    {"static", kmp_sch_static_chunked, omp_sched_static},
    // Ignore monotonicity specification on static schedules
    {"monotonic:static", kmp_sch_static,
     omp_sched_t(omp_sched_static | omp_sched_monotonic)},
    {"nonmonotonic:static", kmp_sch_static, omp_sched_static},
    // Auto => static internally
    {"auto", kmp_sch_static, omp_sched_auto},
    // Ignore specification of monotonicity on guided scheules.
    {"guided", kmp_sch_guided_chunked, omp_sched_guided},
    {"monotonic:guided", kmp_sch_guided_chunked,
     omp_sched_t(omp_sched_guided | omp_sched_monotonic)},
    {"nonmonotonic:guided", kmp_sch_guided_chunked, omp_sched_guided},
    // Observe monotonicity on dynamic schedule.
    {"dynamic",
     kmp_sched_t(kmp_sch_modifier_nonmonotonic | kmp_sch_dynamic_chunked),
     omp_sched_dynamic},
    {"nonmonotonic:dynamic",
     kmp_sched_t(kmp_sch_modifier_nonmonotonic | kmp_sch_dynamic_chunked),
     omp_sched_dynamic},
    {"monotonic:dynamic",
     kmp_sched_t(kmp_sch_modifier_monotonic | kmp_sch_dynamic_chunked),
     omp_sched_t(omp_sched_dynamic | omp_sched_monotonic)},
    // Our debug testing version. Non-monotic, but start with all the work in one place!
    {"imbalanced", kmp_sch_imbalanced, lomp_sched_imbalanced},
};
enum { numSchedules = sizeof(schedules) / sizeof(schedules[0]) };

static omp_sched_t lookupSchedule(std::string const & candidate) {
  for (int i = 0; i < numSchedules; i++) {
    if (schedules[i].name == candidate) {
      return schedules[i].externalValue;
    }
  }
  printWarning("%s is not a schedule understood by the runtime. Using "
               "schedule(static).");
  return omp_sched_static;
}

static omp_sched_t externaliseSchedule(kmp_sched_t internal) {
  for (int i = 0; i < numSchedules; i++) {
    if (schedules[i].internalValue == internal) {
      return schedules[i].externalValue;
    }
  }
  fatalError("Unknown internal (kmp_sched_t) schedule 0x%x when converting to "
             "external form.",
             internal);
  return omp_sched_static;
}

static kmp_sched_t internaliseSchedule(omp_sched_t external) {
  for (int i = 0; i < numSchedules; i++) {
    if (schedules[i].externalValue == external) {
      return schedules[i].internalValue;
    }
  }
  fatalError("Unknown external (omp_sched_t) schedule 0x%x when converting to "
             "internal form.",
             external);
  return kmp_sch_static;
}

static char const * internalName(int32_t internal) {
  for (int i = 0; i < numSchedules; i++) {
    if (schedules[i].internalValue == internal) {
      return schedules[i].name;
    }
  }
  // runtime is not in the table since it makes no sense for the user to set it
  // to resolve the type of a runtime schedule!
  return internal == kmp_sch_runtime ? "runtime" : "unknown schedule";
}

// Extract the scheduling ICVs from the current team.
void getScheduleInfo(omp_sched_t * schedp, int * chunkp) {
  auto team = Thread::getCurrentThread()->getTeam();
  *schedp = externaliseSchedule(team->getRuntimeSchedule());
  *chunkp = team->getRuntimeLoopChunk();
}

// Set the scheduling ICVs in the current team.
void setScheduleInfo(omp_sched_t sched, int chunk) {
  kmp_sched_t internal = internaliseSchedule(sched);
  if (internal == kmp_sch_static && chunk != 0) {
    internal = kmp_sch_static_chunked;
  }
  // Should this be thread safe? Because this isn't...
  Thread::getCurrentThread()->getTeam()->setRuntimeSchedule(internal, chunk);
  // Do this after the getCurrentThread call, since that may be where
  // the runtime is initialized.
  debug(Debug::Loops, "Setting schedule %s, %d", internalName(internal), chunk);
}

// Read the envirable and parse the result
void initializeLoops() {
  std::pair<std::string, int> values;
  if (environment::getStringWithIntArgument("OMP_SCHEDULE", values, {"", 0})) {
    setScheduleInfo(lookupSchedule(values.first), values.second);
  }
}

template <typename loopVarType>
bool canonicalLoop<loopVarType>::forStaticInit(int32_t schedtype,
                                               int32_t * plastIter,
                                               loopVarType * plower,
                                               loopVarType * pupper,
                                               loopVarType * pstride) {
  // Check for zero trip loop before doing anything else
  if (count == 0) {
    if (plastIter)
      *plastIter = false;
    *pstride = incr;
    return false;
  }

  // There is work to do so we need to find out who we are and so on; we assume that
  // it is cheap to call Thread::getCurrentThread(), so it's not worth passing in the
  // global thread ID which the compiler passed us.
  auto myThread = Thread::getCurrentThread();
  auto me = myThread->getLocalId();
  auto numThreads = myThread->getTeam()->getCount();
  auto wholeIters = count / numThreads;
  auto leftover = count % numThreads;

  debug(Debug::Loops,
        "%d/%d: forStaticInit:"
        " schedule(%s),base %d, end %d, incr %d, scale %d, count %d",
        me, numThreads, internalName(schedtype), base, end, incr, scale, count);

  // Check the schedule. We don't have to worry about
  // schedule(runtime) here, since the code generator has to
  // generate schedule(runtime) on the assumption that it will be a
  // dynamic schedule, so this may be called from the dynamic init
  // function, but it controls that and we don't need to check here.
  switch (schedtype) {
  case kmp_sch_static: {
    // One contiguous chunk per thread balanced as well as possible.
    // Hand out remainder iterations one to each thread to maintain balance
    // as best as we can.
    loopVarType myBase;
    loopVarType extras;

    if (me < leftover) {
      myBase = me * (wholeIters + 1);
      extras = 1;
    }
    else {
      myBase = me * wholeIters + leftover;
      extras = 0;
    }
    // With this schedule the highest thread always executes the final chunk,
    // unless we have fewer iterations than threads...
    if (plastIter) {
      if (UNLIKELY(count < numThreads)) {
        *plastIter = (me == (count - 1));
      }
      else {
        *plastIter = (me == (numThreads - 1));
      }
    }
    *plower = getChunkLower(myBase);
    *pupper = getChunkUpper(myBase + wholeIters - 1) + extras * incr;
    *pstride = count;
    debug(Debug::Loops,
          "%d/%d: forStaticInit(blocked) => lower %d, upper %d, stride %d, "
          "last %d",
          me, numThreads, *plower, *pupper, *pstride, *plastIter);
    break;
  }
  case kmp_sch_static_chunked: {
    // Block cyclically over the threads; this exporession works for the statically compiled in
    // schedule, but not for the same schedule when invoked (via schedule(runtime) from the
    // dynamic runtime scheduling interface.
    // There we have to return one chunk per call to the dispatch_next function...
    *pstride = numThreads * scale;
    *plower = base + me * scale;
    *pupper = base + (me + 1) * scale - incr;
    if (plastIter) {
      *plastIter = (me == ((count - 1) % numThreads));
    }
    debug(Debug::Loops,
          "%d/%d: forStaticInit(cyclic) => lower %d, upper %d, stride %d "
          "last %d",
          me, numThreads, *plower, *pupper, *pstride, *plastIter);
    break;
  }
  default:
    fatalError("Unknown static schedule 0x%x", schedtype);
    break;
  }
  return (count > me);
}

// Dynamic loops
template <typename loopVarType>
static void computeDynamicLoopParams(ThreadTeam * team, dynamicLoop * theLoop,
                                     int32_t schedule, loopVarType lb,
                                     loopVarType ub, loopVarType incr,
                                     loopVarType chunk) {
  typedef typename typeTraits_t<loopVarType>::unsigned_t unsignedType;

  // Handle runtime schedule, converting it into something more solid!
  auto schedNM = SCHEDULE_WITHOUT_MODIFIERS(schedule);
  if (UNLIKELY(schedNM == kmp_sch_runtime)) {
    schedule = team->getRuntimeSchedule();
    schedNM = SCHEDULE_WITHOUT_MODIFIERS(schedule);
    chunk = team->getRuntimeLoopChunk();
  }

  // Handle auto schedule
  // Map it to static if it requires monotonicity, nonmonotonic:dynamic otherwise
  if (UNLIKELY(schedNM == kmp_sch_auto)) {
    if (!SCHEDULE_HAS_MONOTONIC(schedule)) {
      schedule = kmp_sch_modifier_nonmonotonic | kmp_sch_dynamic_chunked;
    }
    else {
      schedule = (chunk == 0) ? kmp_sch_static : kmp_sch_static_chunked;
    }
  }

  if (UNLIKELY(chunk == 0)) {
    chunk = 1;
  }
  auto threadCount = team->getCount();
  // If we only have one thread a blocked static schedule is semantically the same as any other,
  // but faster to schedule.
  if (UNLIKELY(threadCount == 1)) {
    schedule = kmp_sch_static;
  }
  theLoop->setSchedule<loopVarType>(schedule);

  // Initialize the canonical, internal, form
  auto cl = theLoop->getCanonicalLoop<loopVarType>();
  cl->init(lb, ub, incr, chunk);
  // Remember the current shared state
  *theLoop->getNextIterationPtr() = 0;

  // For schedules which have per-thread data and that is accessed by
  // other threads, we must initialize that for all threads here,
  // since otherwise threads which have entered the loop may look at
  // data for threads which haven't got here yet!  Note that this
  // could potentially be done on demand, rather than eagerly, which
  // would allow it to be better parallelized. Instead of initializing
  // them all here, each thread would initialize it's own value when
  // it checked into the loop if it hadn't yet been initialized by
  // another thread at the point it was trying to steal from the
  // uninitialized data.
  // For now we just serialize and do it all here.
  if (schedule == kmp_sch_imbalanced ||
      schedule == (kmp_sch_dynamic_chunked | kmp_sch_modifier_nonmonotonic)) {
    auto loopIdx = theLoop->getLoopIdx();
    auto iterations = cl->getCount();

    if (schedule == kmp_sch_imbalanced) {
      for (uint32_t t = 0; t < threadCount; t++) {
        auto thread = team->getThread(t);
        auto work = thread->getPackedWork(loopIdx)->getWork(unsignedType(0));
        if (t == 0) {
          // Give all the work to thread zero.
          work->assign(0, iterations);
        }
        else {
          work->assign(0, 0);
        }
        work->zeroStarted();
      }
      debug(Debug::Loops, "Initialised all descriptors for imbalanced loop[%u]",
            loopIdx);
    }
    else {
      for (uint32_t t = 0; t < threadCount; t++) {
        auto thread = team->getThread(t);
        auto work = thread->getPackedWork(loopIdx)->getWork(unsignedType(0));
        work->initializeBalanced(iterations, t, threadCount);
        work->zeroStarted();
      }
      debug(Debug::Loops,
            "Initialised all descriptors for nonmonotonic loop[%u]", loopIdx);
    }
  }
  debug(Debug::Loops,
        "%d/%d: computeDynamicLoopParams (%s): lb %d, ub %d, incr, %d chunk %d",
        Thread::getCurrentThread()->getLocalId(), team->getCount(),
        internalName(schedule), lb, ub, incr, chunk);
}

template <typename loopVarType>
// We don't make this a member of the canonicalLoop, because most threads won't need to create a
// cannonicalLoop here, since we only need one which is shared.
static void initDynamicLoop(int32_t schedule, loopVarType lb, loopVarType ub,
                            loopVarType incr, loopVarType chunk) {
  auto myThread = Thread::getCurrentThread();
  auto myTeam = myThread->getTeam();
  auto myLoopCount = myThread->getDynamicLoopCount();
  auto theLoop = myTeam->getLoop(myThread->getDynamicLoopIndex());

  debug(Debug::Loops, "%d/%d: initDynamicLoop (%s): loop %d",
        myThread->getLocalId(), myTeam->getCount(), internalName(schedule),
        myLoopCount);

  // The pointer will be correct even if the target hasn't yet been
  // intialized, so we can hoist this up here since we won't look at

  // it until we have checked the target has been set up.
  myThread->setCurrentLoop(theLoop);

  // Has the loop descriptor already been initialized?  Since only one
  // thread in the team has to do this it's likely already to have been
  // done.
  if (UNLIKELY(theLoop->isUninitialized(myLoopCount))) {
    // It hasn't been initialized, but someone may already be working on it.
    if (theLoop->claim(myLoopCount)) {
      // We are first and must initialize the loop data
      auto threadCount = myTeam->getCount();
      theLoop->setThreadCount(threadCount);
      computeDynamicLoopParams(myTeam, theLoop, schedule, lb, ub, incr, chunk);
      // Tell any other threads which may be waiting that the loop data is complete
      // so they can proceed into the loop.
      theLoop->completeInitialization(threadCount);
    }
    else {
      // Some other thread is in the process of initializing the descriptor,
      // so we need to wait until they have done that before we can continue.
      // (Checking and waiting here seems better than doing it in every call to
      // get the next set of iterations...)
      while (theLoop->isUninitialized(myLoopCount)) {
        // Could implement backoff and so on here...
        Target::Yield();
      }
    }
  }

  // If this is a schedule which needs local, unshared, per-thread
  // information, we also need to initialize that.  N.B. the schedule
  // passed in may be schedule(runtime). That will have been resolved
  // in the initialization, so we must read the true schedule back
  // from there!  (Well worth a confused hour :-( wondering why this
  // initialization wasn't happening!)
  //
  // This initialization is only for static schedules where there is
  // per-thread state, but it is not shared with other threads. For
  // static steal, the per-thread state for all threads has to be
  // initialized before any thread can enter the loop.
  schedule = theLoop->getSchedule();

  switch (schedule) {
  case kmp_sch_static:
    // The static, single block/thread schedule.
    myThread->setNextLoopChunk(0);
    break;

  case kmp_sch_static_chunked:
    // Static cyclic, so we always start with a chunk which is the
    // same as our local thread ID.  It doesn't matter here if this is
    // above the number of chunks we have. That'll get sorted out when
    // we hand out iterations later.
    //
    // Note that this requires a call into the runtime for each chunk
    // handed out here, even though when treated at compile time as a
    // static schedule only one runtime call is required.  That makes
    // schedule(runtime), omp_set_schedule(omp_sched_static,1),
    // potentially perform worse than schedule(static,1) visible at
    // compile time.
    myThread->setNextLoopChunk(myThread->getLocalId());
    break;

  default:
    break;
  }

  debug(Debug::Loops,
        "%d/%d: initDynamicLoop (%s): saw loop %d initialized: done",
        myThread->getLocalId(), myTeam->getCount(), internalName(schedule),
        myLoopCount);
}

template <typename unsignedType>
void contiguousWork<unsignedType>::initializeBalanced(unsignedType count,
                                                      uint32_t me,
                                                      uint32_t numThreads) {
  // Set up properties for the static, maximal chunk, work allocation.
  // Note that the internal representation here is based on <, rather
  // than <= as in the canonical form. That lets (n,n) mean no work,
  // which is convenient here, since it can arise if the first thread
  // has all its work stolen before it arrives.
  auto wholeIters = count / numThreads;
  auto leftover = count % numThreads;
  unsignedType b;
  unsignedType e;
  if (me < leftover) {
    b = me * (wholeIters + 1);
    e = b + wholeIters + 1;
  }
  else {
    b = me * wholeIters + leftover;
    e = b + wholeIters;
  }
  setBase(b, std::memory_order_relaxed);
  setEnd(e, std::memory_order_relaxed);
}

// Provide separate functions for each of the possible schedules to dispatch
// the next set of iterations.
// We then store a function pointer in the loop data for the current loop, so
// make the code more modular, functions smaller, and (we hope) avoid
// branch misprediction.

// Loop dispatch functions for each schedule.

// Static well balanced. Can be handled here as handing out one chunk and then we're done.
template <typename loopVarType>
int32_t dynamicLoop::dispatchStatic(Thread * myThread, int32_t * p_last,
                                    loopVarType * p_lb, loopVarType * p_ub,
                                    loopVarType * p_st) {
  if (myThread->getNextLoopChunk() == 0) {
    myThread->setNextLoopChunk(1);
    return getCanonicalLoop<loopVarType>()->forStaticInit(schedule, p_last,
                                                          p_lb, p_ub, p_st);
  }
  else {
    return false;
  }
}

// Static cyclic. We have to hand out each chunk separately.
template <typename loopVarType>
int32_t dynamicLoop::dispatchStaticChunked(Thread * myThread, int32_t * p_last,
                                           loopVarType * p_lb,
                                           loopVarType * p_ub,
                                           loopVarType * p_st) {
  // Each chunk is treated as a separate loop...
  auto myChunk = myThread->getNextLoopChunk();
  auto cl = getCanonicalLoop<loopVarType>();

  debug(Debug::Loops, "%d/%d: dispatchIterations(static_chunked): myChunk %u",
        myThread->getLocalId(), threadCount, myChunk);
  if (myChunk >= cl->getCount()) {
    return false;
  }
  else {
    *p_lb = cl->getChunkLower(myChunk);
    *p_ub = cl->getChunkUpper(myChunk);
    *p_st = threadCount * cl->getStride(myChunk, myChunk + threadCount);
    if (p_last) {
      *p_last = cl->isLastChunk(myChunk);
    }
    myThread->setNextLoopChunk(myChunk + threadCount);
    return true;
  }
}

// Guided
template <typename loopVarType>
int32_t dynamicLoop::dispatchGuided(Thread * myThread, int32_t * p_last,
                                    loopVarType * p_lb, loopVarType * p_ub,
                                    loopVarType * p_st) {
  // Suppress "unused parameter" warning when compiling with DEBUG=0.
  // In C++17 should use [[maybe_unused]] in the declaration above.
  (void)myThread;
  auto cl = getCanonicalLoop<loopVarType>();

  for (;;) {
    auto localNextIteration = nextIteration.load();
    auto remaining = cl->getCount() - localNextIteration;
    if (remaining == 0) {
      return false;
    }
    // What is my share of the remaining iterations?
    auto myShare = (remaining + threadCount - 1) / threadCount;
    // Arbitrarily choose to take 1/2 of it.
    auto delta = (myShare + 1) / 2;
    debug(Debug::Loops,
          "%d/%d: guided count %u, next %u, remaining %u, delta %u",
          myThread->getLocalId(), threadCount, cl->getCount(),
          localNextIteration, remaining, delta);
    if (nextIteration.compare_exchange_strong(localNextIteration,
                                              localNextIteration + delta)) {
      auto lastIteration = localNextIteration + delta - 1;
      *p_lb = cl->getChunkLower(localNextIteration);
      *p_ub = cl->getChunkUpper(lastIteration);
      *p_st = cl->getStride(localNextIteration, lastIteration);
      if (p_last) {
        *p_last = cl->isLastChunk(lastIteration);
      }
      return true;
    }
    // Compare exchange failed, should maybe perform better backoff...
    Target::Yield();
  }
}

template <typename loopVarType>
int32_t dynamicLoop::dispatchMonotonic(Thread *, int32_t * p_last,
                                       loopVarType * p_lb, loopVarType * p_ub,
                                       loopVarType * p_st) {
  auto cl = getCanonicalLoop<loopVarType>();
  for (;;) {
    auto localNI = nextIteration.load();
    if (UNLIKELY(localNI == cl->getCount())) {
      // No iterations remain.
      return false;
    }
    // Using compare_exchange rather than add avoids going past the end, which could, potentially
    // be a problem if iterating over the full range of the type.
    if (nextIteration.compare_exchange_strong(localNI, localNI + 1)) {
      *p_lb = cl->getChunkLower(localNI);
      *p_ub = cl->getChunkUpper(localNI);
      *p_st = cl->getStride(localNI, localNI);
      if (p_last) {
        *p_last = cl->isLastChunk(localNI);
      }
      return true;
    }
    // Compare exchange failed, should maybe perform better backoff...
    Target::Yield();
  }
}

template <typename loopVarType>
int32_t dynamicLoop::dispatchNonmonotonic(Thread * myThread, int32_t * p_last,
                                          loopVarType * p_lb,
                                          loopVarType * p_ub,
                                          loopVarType * p_st) {
  // Static steal. Stealing scheme can certainly be improved.
  typedef typename typeTraits_t<loopVarType>::uint_t unsignedType;
  auto me = myThread->getLocalId();
  auto myWork = myThread->getPackedWork()->getWork(unsignedType(0));
  auto myTeam = myThread->getTeam();
  auto cl = getCanonicalLoop<loopVarType>();
  debug(Debug::Loops, "%d (%s): base %u, end %u, count %u", me,
        internalName(schedule), myWork->getBase(), myWork->getEnd(),
        myWork->getIterations());

  // Normal case, we hope, is that we have local work.
  unsignedType nextIteration;
  if (LIKELY(myWork->incrementBase(&nextIteration))) {
    myWork->incrStarted();
    *p_lb = cl->getChunkLower(nextIteration);
    *p_ub = cl->getChunkUpper(nextIteration);
    *p_st = cl->getStride(nextIteration, nextIteration);
    if (p_last) {
      *p_last = cl->isLastChunk(nextIteration);
    }
    debug(Debug::Loops, "%d: found local work %d (my count now %d)", me,
          nextIteration, myWork->getStarted());
    return true;
  }
  // No local work, so we must look for some elsewhere.
  // Another thread has already noticed there are no remaining iterations to be found.
  // So there's no point even looking for them.
  if (isFinished()) {
    return false;
  }
  // Choose a random victim to start from; if we decide to steal from
  // ourself we just choose again (we could go to our successor, but
  // that would raise their probability to twice that of anyone else,
  // and stealing is expensive anyway).
  debug(Debug::Loops, "%d: finding random victim", me, threadCount);
  uint32_t victim;
  do {
    victim = myThread->nextRandom() % threadCount;
  } while (victim == me);

  // Remember the total amount of work we're looking for.
  auto totalIterations = cl->getCount();
  auto loopIdx = myThread->getDynamicLoopIndex();

  debug(Debug::Loops, "%d: total iterations %d", me, totalIterations);
  // Lock our descriptor so that no one looks at it while we're messing with it.
  // There's nothing to steal there anyway...
  myWork->setStealing();

  // While there's still work to be found.
  while (!isFinished()) {
    // Keep count of the work which has already started execution.
    auto iterationsStarted = myWork->getStarted();
    debug(Debug::Loops, "%d: Starting steal loop mine %d/total %d", me,
          iterationsStarted, totalIterations);
    // Try each possible victim sequentially from a random starting point.
    for (unsigned int i = 0; i < threadCount; i++) {
      auto v = (victim + i) % threadCount;
      if (v == me) {
        // I know I have no work, and I can't steal from myself anyway!
        continue;
      }
      auto otherWork = myTeam->getThread(v)->getPackedWork(loopIdx)->getWork(
          unsignedType(0));
      // Count the iterations the victim thread has started.
      // A possible improvement here might be to defer this until we
      // have done one pass and seen no work, since that way we
      // won't move the counters into shared state until near the
      // end of the computation.
      iterationsStarted += otherWork->getStarted();
      debug(Debug::Loops,
            "%d: considering victim %d which has done %d iterations; total now "
            "%d of %d",
            me, v, otherWork->getStarted(), iterationsStarted, totalIterations);

      LOMP_ASSERT(iterationsStarted <= totalIterations);

      if (UNLIKELY(iterationsStarted == totalIterations)) {
        // There is no more work to be found.
        // Tell everyone else.
        setFinished();
        myWork->clearStealing();
        debug(Debug::Loops, "%d: all work started", me, threadCount);
        return false;
      }

      // Does this look a good victim? Not if it's also trying to steal!
      if (otherWork->isStealing()) {
        debug(Debug::Loops, "%d: skip %d as it's stealing too", me, v);
        continue;
      }
      unsignedType stolenB;
      unsignedType stolenE;
      if (otherWork->trySteal(&stolenB, &stolenE)) {
        // We've started the lowest one we just stole (which we won't reveal
        // as being available).
        myWork->incrStarted();
        // This thread now owns the work it just stole, so remember that for later
        // and so that it can be stolen from us if need be.
        myWork->assign(stolenB + 1, stolenE);
        // Let other threads steal from us again...
        myWork->clearStealing();
        // Return the lowest piece of work which we didn't make visible,
        // so that this thread can execute it.
        *p_lb = cl->getChunkLower(stolenB);
        *p_ub = cl->getChunkUpper(stolenB);
        *p_st = cl->getStride(stolenB, stolenB);
        if (p_last) {
          *p_last = cl->isLastChunk(stolenB);
        }
        debug(Debug::Loops, "%d: succesfuly stole(%u:%u) from %d, returning %d",
              me, stolenB, stolenE, v, stolenB);
        return true;
      }
      else {
        debug(Debug::Loops, "%d: failed to steal from %d", me, v);
      }
    } // Loop over potential victims
  }   // While there's work
  myWork->clearStealing();
  debug(Debug::Loops, "%d: saw loop as finished", me);
  return false; // If we got here there is no work available anywhere.
}

template <typename loopVarType>
void dynamicLoop::setSchedule(int32_t sched) {
  // Set the schedule.
  schedule = sched;

  // And fill in the dispatch function.
  switch (schedule) {
  case kmp_sch_static:
  case kmp_sch_static | kmp_sch_modifier_monotonic:
  case kmp_sch_static | kmp_sch_modifier_nonmonotonic:
    setDispatchFunction(&dynamicLoop::dispatchStatic<loopVarType>);
    break;
  case kmp_sch_static_chunked:
  case kmp_sch_static_chunked | kmp_sch_modifier_monotonic:
  case kmp_sch_static_chunked | kmp_sch_modifier_nonmonotonic:
    setDispatchFunction(&dynamicLoop::dispatchStaticChunked<loopVarType>);
    break;
  case kmp_sch_guided_chunked:
  case kmp_sch_guided_chunked | kmp_sch_modifier_monotonic:
  case kmp_sch_guided_chunked | kmp_sch_modifier_nonmonotonic:
    setDispatchFunction(&dynamicLoop::dispatchGuided<loopVarType>);
    break;
  case kmp_sch_dynamic_chunked:
  case kmp_sch_dynamic_chunked | kmp_sch_modifier_monotonic:
    setDispatchFunction(&dynamicLoop::dispatchMonotonic<loopVarType>);
    break;
  case kmp_sch_imbalanced:
  case kmp_sch_dynamic_chunked | kmp_sch_modifier_nonmonotonic:
    setDispatchFunction(&dynamicLoop::dispatchNonmonotonic<loopVarType>);
    break;
  default:
    fatalError("schedule(%s) 0x%x not yet supported", internalName(schedule),
               schedule);
  }
}

// Do a local increment (which need not be atomic), but check
// afterwards to see whether someone stole from us. This should be
// faster than the code above, since it means that stealing need not
// cause interference here unless the stealing thread really took the
// final iteration.  We need to be careful throughout, now, though,
// since this can lead to a sequence that ends up with (base, end) ==
// (1,0) (for instance). We must handle that case and recognise that
// it means "no work".
template <typename unsignedType>
bool contiguousWork<unsignedType>::incrementBase(unsignedType * basep) {
  auto oldBase = getBase();
  auto oldEnd = getEnd();

  // Have we run out of iterations?
  if (UNLIKELY(oldBase >= oldEnd)) {
    debug(Debug::Loops,
          "%d: no local iterations available(%u:%u) => no iterations available",
          Thread::getCurrentThread()->getLocalId(), oldBase, oldEnd);
    return false;
  }
  // Update the base value.
  // We need to ensure that the subsequent load does not float above this,
  // so need sequential consistency to prevent that from happening.
  // (Which took me about a week to work out...)
  // Release consistency ensures earlier stores are complete, but does
  // not prevent the load from floating up above the store.
  setBase(oldBase + 1, std::memory_order_seq_cst);

  // Load the end again, so that we can see if it changed while we were
  // incrementing the base. This thread never moves the end, but other
  // threads do.
  auto newEnd = getEnd();

  // Did someone steal the work we thought we were going to take?
  if (UNLIKELY(newEnd == oldBase)) {
    // Someone stole our last iteration while we were trying to claim it.
    debug(Debug::Loops,
          "%d: last local iterations stolen (%u:%u) => no iterations available",
          Thread::getCurrentThread()->getLocalId(), oldBase, newEnd);
    return false;
  }

  // We got it. It doesn't matter if the end moved down while we were
  // incrementing the base, as long as it is still above the base we
  // claimed.  (Say we're claiming iteration zero, while some other thread
  // steals iterations [100,200], that's fine. It doesn't impact on us
  // claiming iteration zero.)
  debug(Debug::Loops, "%d: got local iteration %d",
        Thread::getCurrentThread()->getLocalId(), oldBase);
  *basep = oldBase;
  return true;
}

// Try to steal from this chunk of work.
//
// It may be possible to be smarter here too, and avoid the use of the double-width
// compare-exchange.
// We would still need a single-width cmpxchg to handle the race between multiple
// stealing threads all trying to steal from teh same victin at the same time,
// but the other case (where the owner thread is incrementing the base while we
// are pulling down the end) should be amenable to teh same type of logic that
// we have in the increment. I.e. perform the steal (move the end down) and then check whether
// the base has moved up to a point that conflicts with what we just did.
// The main problem there is likely that we could move down well below where
// we ought to be (if the owner gets in a lot of increments between us reading
// the end and lowering it), so we'd have to be carefule about our comparisons.
// Of course, this may all be utterly in the nooise for real code performance!
//
template <typename unsignedType>
bool contiguousWork<unsignedType>::trySteal(unsignedType * basep,
                                            unsignedType * endp) {
  // Load this once; after that the compare_exchange updates it.
  contiguousWork oldValues(this);
  for (;;) {
    // oldValues is local, so no-one else is looking at it and
    // we can use a relaxed memory order here.
    auto oldBase = oldValues.getBase(std::memory_order_relaxed);
    auto oldEnd = oldValues.getEnd(std::memory_order_relaxed);

    // We need this >= to handle the race resolution case mentioned above,
    // which can lead to (1,0) (or equivalent) appearing...
    if (UNLIKELY(oldBase >= oldEnd)) {
      debug(Debug::Loops,
            "%d: failed to steal (%u:%u) => no iterations available",
            Thread::getCurrentThread()->getLocalId(), oldBase, oldEnd);
      return false;
    }
    // Try to steal half of the available work, By doing that we
    // distribute stealable work across the machine, reducing
    // contention and meaning that stealing threads need to look less
    // far to find it.
    // Other options are also possible, of course; however, which is optimal
    // is likely to be workload dependent, and stealing half seems a reasonable
    // approach.
    auto available = oldEnd - oldBase;
    // Round up so that we will steal the last available iteration.
    auto newEnd = oldEnd - (available + 1) / 2;
    contiguousWork newValues(oldBase, newEnd);

    // Did anything change while we were calculating our parameters?
    // This is slightly over-cautious. If we're stealing iterations
    // 1000:2000 it doesn't matter if the owner claims iteration zero, so
    // we might be able to be smarter about this.
    // Since this is inside a retry loop we can use the weak compare_exchange
    // as recommended by the C++ folk. (It may sometimes fail when it could
    // succeeed, but is still supposedly more efficient)
    if (atomicPair.compare_exchange_weak(oldValues.pair, newValues.pair)) {
      // Compare exchange succeeded, so nothing changed while we were thinking about this
      // and we have successfully stolen the value and updated the shared state.
      *basep = newEnd;
      *endp = oldEnd;
      debug(Debug::Loops, "%d: stole(%u:%u) left (%u:%u)",
            Thread::getCurrentThread()->getLocalId(), newEnd, oldEnd, oldBase,
            newEnd);
      return true;
    }
    else {
      // The compare_exchange failed, so someone else changed something (or
      // the system just doesn't like us, since we're using the weak version!)
      // The compare_exchange updates oldValues, so we can go around and try again
      // without explicitly reloading them ourselves.
      debug(Debug::Loops, "%d: steal failed",
            Thread::getCurrentThread()->getLocalId());
    }
  }
}
} // namespace lomp

// Interface functions called by the compiler.
#define generateForStaticInit(width, type, arg)                                \
  void __kmpc_for_static_init_##width(                                         \
      ident_t *, int32_t, int32_t schedtype, int32_t * plastIter,              \
      type * plower, type * pupper, type * pstride, type incr, type chunk) {   \
    lomp::canonicalLoop<type> loopInfo(*plower, *pupper, incr, chunk);         \
                                                                               \
    loopInfo.forStaticInit(schedtype, plastIter, plower, pupper, pstride);     \
  }

// Interface functions called by the compiler.
#define generateForDynamicInit(width, type, arg)                               \
  void __kmpc_dispatch_init_##width(ident_t *, int32_t, int32_t schedule,      \
                                    type lb, type ub, type incr, type chunk) { \
    lomp::initDynamicLoop(schedule, lb, ub, incr, chunk);                      \
  }

#define generateForDynamicNext(width, type, arg)                               \
  int32_t __kmpc_dispatch_next_##width(ident_t *, int32_t, int32_t * p_last,   \
                                       type * p_lb, type * p_ub,               \
                                       type * p_st) {                          \
    auto myThread = lomp::Thread::getCurrentThread();                          \
    auto theLoop = myThread->getCurrentLoop();                                 \
    auto dispatchFunction = theLoop->getDispatchFunction<type>();              \
    auto haveIterations =                                                      \
        (theLoop->*dispatchFunction)(myThread, p_last, p_lb, p_ub, p_st);      \
    if (LIKELY(haveIterations)) {                                              \
      return true;                                                             \
    }                                                                          \
    else {                                                                     \
      myThread->endDynamicLoop();                                              \
      return false;                                                            \
    }                                                                          \
  }

// The compiler seems erratic in calling dispatch_fini (LLVM doesn't
// seem to!), so we do nothing there.  We already know when each
// thread leaves the loop because we told it to do so!
#define generateForDynamicFini(width, type, arg)                               \
  void __kmpc_dispatch_fini__##width(ident_t *, int32_t) {}

extern "C" {
// clang-format off
/// Expand all of the loop functions
#if LOMP_HAVE_INT128T
#define FOREACH_LOOPTYPE(macro, arg)            \
  macro(4, int32_t, arg)                        \
  macro(4u, uint32_t, arg)                      \
  macro(8, int64_t, arg)                        \
  macro(8u, uint64_t, arg)
#else
#define FOREACH_LOOPTYPE(macro, arg)            \
  macro(4, int32_t, arg)                        \
  macro(4u, uint32_t, arg)
#endif
// clang-format on

FOREACH_LOOPTYPE(generateForStaticInit, 0)
FOREACH_LOOPTYPE(generateForDynamicInit, 0)
FOREACH_LOOPTYPE(generateForDynamicNext, 0)
FOREACH_LOOPTYPE(generateForDynamicFini, 0)

// No need to do anything here. (Though if we were supporting profiling we would have something...)
void __kmpc_for_static_fini(ident_t *, int32_t) {}
} // extern "C"
