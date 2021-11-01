//===-- threads.h - Interface for thread handling ---------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of threads and teams.
///
//===----------------------------------------------------------------------===//
#ifndef THREADS_H_INCLUDED
#define THREADS_H_INCLUDED
#include <cstdlib>
#include <cstdint>
// C++11 Thread support; we may want to use C11 instead if this brings in the whole of the C++ runtime.
// (Or just revert to pthreads...)
#include <thread>
#include <atomic>
#include <cstdarg>

#include "interface.h"
#include "debug.h"
#include "target.h"
#include "barriers.h"
#include "loops.h"
#include "mlfsr32.h"

namespace lomp {

namespace Tasking {
struct TaskDescriptor;
struct TaskPool;
struct Taskgroup;
TaskPool * TaskPoolFactory();
} // namespace Tasking

class Thread;
class ThreadTeam;

// Packed arguments needed for the thread constructor.
// We want to allocate and initialize the Thread
// while executing inside the appropriate system thread,
// so need to pass these over.
struct ThreadArgs {
  ThreadTeam * T;
  int L;
  int G;
  bool M;
  std::thread * SysThread;
  ThreadArgs(ThreadTeam * team, int localId, int globalId, bool Master,
             std::thread * S)
      : T(team), L(localId), G(globalId), M(Master), SysThread(S){};
  ThreadArgs() {}
};

class ThreadTeam {
  Thread ** Threads;
  uint32_t NThreads;
  std::atomic<uint32_t> threadsCreated;
  // Ideally we'd have the barrier data here, but for now, since we want to make it easy to play
  // with different barrier implementations, we don't.
  Barrier * ForkJoinBarrier;
  // Is the team executing in parallel, or are most threads waiting at a fork barrier?
  bool Parallel;

  // Information about loops
  // The implementation to be used for schedule(runtime), set either by parsing OMP_SCHEDULE,
  // or the user calling omp_set_schedule().
  kmp_sched_t RuntimeLoopSchedule;
  uint32_t RuntimeLoopChunk;

  // Loop descriptors for dynamic loops. We need more than one to handle "nowait" loops.
  dynamicLoop loops[Max_Concurrent_Loops];

  // The number of single constructs which have been started in the team.
  // 64b out of paranoia, since a 32b counter would wrap after 4.3s at one
  // tick/ns, whereas a 64b one takes ~580 years, which seems safe.
  std::atomic<uint64_t> NextSingle;

public:
  std::atomic<ssize_t> activeTasks;

  ThreadTeam(int ThreadCount);
  auto getBarrier() const {
    return ForkJoinBarrier;
  }
  auto inParallel() const {
    return Parallel;
  }
  void incCreated() {
    threadsCreated++;
  }
  void enterParallel() {
    Parallel = true;
  }
  void leaveParallel() {
    Parallel = false;
  }
  auto getCount() const {
    return NThreads;
  }
  auto getThread(uint32_t threadID) const {
    return Threads[threadID];
  }
  void setThread(uint32_t threadID, Thread * t) {
    Threads[threadID] = t;
  }
  void waitForCreation() {
    // Using this trivial barrier seems OK, since the cost of thread creation
    // in the OS is already large!
    debug(Debug::Threads, "See %d/%d", int(threadsCreated), NThreads);
    while (threadsCreated != NThreads) {
      Target::Yield();
    }
  }
  // The loop count is masked to ensure this is OK when it is read...
  auto getLoop(uint32_t idx) {
    return &loops[idx];
  }
  auto getRuntimeSchedule() const {
    return RuntimeLoopSchedule;
  }
  auto getRuntimeLoopChunk() const {
    return RuntimeLoopChunk;
  }
  void setRuntimeSchedule(kmp_sched_t sch, uint32_t chunk) {
    RuntimeLoopSchedule = sch;
    RuntimeLoopChunk = chunk;
  }
  // Access to the count of "single" constructs.
  auto tryIncrementNextSingle(uint64_t oldVal) {
    // Use test and test&set, though it probably doesn't really matter.
    // On some processors it avoids the RFO, so may make things
    // slightly faster.
    return (NextSingle.load(std::memory_order_acquire) == oldVal) &&
           NextSingle.compare_exchange_strong(oldVal, oldVal + 1);
  }
};

class CACHE_ALIGNED Thread {
#if LOMP_SERIAL
  static Thread * MyThread; /* How we get here... */
#else
  thread_local static Thread * MyThread; /* How we get here... */
#endif

  // Current team to which this thread belongs.
  ThreadTeam * Team;
  // index within the team (what omp_get_thread_num() will return)
  uint32_t LocalId;

  // Task scheduling information.
  // Pointer to the current task that is being executed
  Tasking::TaskDescriptor * current_task = nullptr;
  Tasking::TaskPool * taskPool;
  Tasking::Taskgroup * taskgroup;

public:
  /* Todo? use accessors for this, so it can be private */
  // counter to be used for taskwaits and implicit tasks.
  std::atomic<ssize_t> childTasks;

private:
  // Dynamic loop scheduling information
  // Number of dynamic loops this thread has completed
  uint32_t DynamicLoopCount;
  // Pointer to the active dynamic loop if the thread is executing one
  dynamicLoop * CurrentLoop;
  // Convenient way to handle schedule(runtime) with static,n schedule
  uint64_t nextLoopChunk;
  // For nonmonotonic:dynamic work stealing implementation.
  packedContiguousWork myWork[Max_Concurrent_Loops];

  // Number of single constructs this thread has seen
  uint64_t SinglesSeen;
  //
  // Reduction implementations
  //
  enum ReductionType {
    UnknownReduction,
    AtomicReduction,
    CriticalSectionReduction,
    TreeReduction
  };

  ReductionType
      CurrentReduction; /* So that we can do the right thing at end_reduce */
  static char const * reductionName(ReductionType t);
  static std::pair<char const *, ReductionType> reductionNames[];
  static ReductionType forcedReduction;

  auto getReduction() const {
    return CurrentReduction;
  }
  void setReduction(ReductionType t) {
    CurrentReduction = t;
  }

  ReductionType chooseReduction(int32_t);

  // These are either rarely used, or used at times when a thread is likely to wait anyway,
  // so can be pushed out to the end into another cache line.
  // The unique global identity of the thread used internally
  uint32_t GlobalId;
  // Per thread random number state; useful for polling and so on.
  mlfsr32 random;
  // The underlying system thread
  std::thread * SystemThread;

  // Static because it's passed to std::thread and called from there, so cannot be
  // a member function which takes an implicit "this" argument.
  static void outerLoop(ThreadArgs const *);
  Thread(ThreadArgs const *);

public:
  // This starts the thread and has it perform its initialization internally,
  // so the returned thread object may not be fully initialized when thnis returns.
  // That doesn't matter, though, the thread will complete initialization before
  // it reaches the barrier to start waiting for work.
  static void createThread(ThreadArgs *);

  void run(InvocationInfo const * What) const {
    What->run((void *)&GlobalId, (void *)&LocalId);
  }
  static auto getCurrentThread() {
    if (UNLIKELY(!RuntimeInitialized))
      initializeRuntime();
    return MyThread;
  }
  auto getLocalId() const {
    return LocalId;
  }
  auto getGlobalId() const {
    return GlobalId;
  }
  auto getTeam() const {
    return Team;
  }
  auto getCurrentLoop() const {
    return CurrentLoop;
  }
  void setCurrentLoop(dynamicLoop * loop) {
    CurrentLoop = loop;
  }
  auto getDynamicLoopCount() const {
    return DynamicLoopCount;
  }
  auto getDynamicLoopIndex() const {
    /* Assume the compiler is smart enough to replace % with & when
`     * dealing with a power of 2.
     */
    return DynamicLoopCount % Max_Concurrent_Loops;
  }
  auto getPackedWork(uint32_t idx) {
    return &myWork[idx];
  }
  auto getPackedWork() {
    return getPackedWork(getDynamicLoopIndex());
  }
  auto nextRandom() {
    return random.getNext();
  }
  void endDynamicLoop() {
    auto d = DynamicLoopCount++;
    debug(Debug::Loops, "%d: releasing loop %u, DynamicLoopCount now %u",
          LocalId, d, DynamicLoopCount);
    // So that it is still used when debug is not enabled...
    (void)d;
    CurrentLoop->decrementUse();
    // Not really necessary but may catch a bug...
    CurrentLoop = 0;
  }
  // Accessors for dynamic loop state associated with this thread.
  // If we introduce nested parallelism these should move to become a per-thread field in the team...
  auto getNextLoopChunk() const {
    return nextLoopChunk;
  }
  void setNextLoopChunk(uint64_t ch) {
    nextLoopChunk = ch;
  }

  // Single count
  auto fetchAndIncrSingleCount() {
    return SinglesSeen++;
  }
  // Tasking related accessors
  auto getCurrentTask() {
    return current_task;
  }
  void setCurrentTask(Tasking::TaskDescriptor * task) {
    current_task = task;
  }
  auto getTaskPool() const {
    return taskPool;
  }
  void setCurrentTaskgroup(Tasking::Taskgroup * tg) {
    taskgroup = tg;
  }
  auto getCurrentTaskgroup() const {
    return taskgroup;
  }

  // Reductions
  // Parses the enviarble and sets the default.
  static void initializeForcedReduction();
  // May need more arguments and state here; we're not supporting tree reductions yet, and we should
  int enterReduction(ident_t *, void *);
  void leaveReduction(void *, bool);
}; // class Thread.

} // namespace lomp

#endif
