//===-- threads.cc - Implementation of thread handling ----------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of threads and thread teams
///
//===----------------------------------------------------------------------===//
#include "threads.h"
#include "tasking.h"
#include "environment.h"
#include "numa_support.h"
#include "locks.h"

namespace lomp {
thread_local Thread * Thread::MyThread;

#if (LOMP_TARGET_LINUX)
#include <sched.h>
// Force thread affinity.

// This is a somewhat dubious thing to do; we should really support
// OpenMP affinity specifications properly. However, this is easy.  It
// gives a compact, ganularity(core) allocation on Intel Xeons and
// also on the Arm machine we use. (On Xeon Phi it would give thread
// (i.e. SMT thread) granularity).  A proper implementation would use
// hwloc and implement all the complexity of the OpenMP standard.
static void forceAffinity(int me) {
  cpu_set_t set;

  // Afinitize the thread to the logicalCPU which has the same number.
  CPU_ZERO(&set);
  CPU_SET(me, &set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0) {
    printWarning("Failed to force affinity for thread %d", me);
  }
  else {
    debug(Debug::Info, "Thread %d tightly affinitized to logicalCPU %d", me,
          me);
  }
}
#else
#define forceAffinity(me) void(0)
#endif

// For now we're just creating a team of the required size.
// Changing team size and so on is omitted.
ThreadTeam::ThreadTeam(int NumThreads)
    : threadsCreated(0), Parallel(false), RuntimeLoopSchedule(kmp_sch_static),
      NextSingle(0), activeTasks(0) {
  // If the user has not requested a specific number of threads,
  // use a number that matches the available hardware.
  // We assume that std::thread handle affinity masks correctly...
  if (NumThreads == 0) {
    NumThreads = std::thread::hardware_concurrency();
  }
  // Remember the number of threads.
  NThreads = NumThreads;
  debug(Debug::Threads, "Total threads %d", NumThreads);

  // Initialize the barrier we will require.
  ForkJoinBarrier = Barrier::newBarrier(NumThreads);

  // Allocate space for the pointers to the threads.
  // Each thread will fill in its own entry.
  Threads = new Thread *[NumThreads];

  // Fill in the initial thread.
  ThreadArgs MainArgs(this, 0, 0, true, 0);
  Thread::createThread(&MainArgs);

  // Then spawn the others. This could be done recursively,
  // but there are kernel locks which mean that's probably not worthwhile,
  // and this only happens once at the moment.
  ThreadArgs args[NumThreads - 1];
  for (int i = 1; i < NumThreads; i++) {
    args[i - 1] = ThreadArgs(this, i, i, false, 0);
    Thread::createThread(&args[i - 1]);
  }

  // Wait until all of the threads has been created.
  // We could use a full barrier here, but need to be careful.
  // We do *not* want to allow the tasking code to run, since it will look at
  // other threads which may not yet have created their Thread data structures.
  debug(Debug::Threads, "Thread 0 waiting for all threads to be created", 0);
  waitForCreation();

  debug(Debug::Threads, "All threads created");
} /* ThreadTeam::ThreadTeam */

void Thread::createThread(ThreadArgs * Args) {
  if (Args->M) {
    new Thread(Args);
  }
  else {
    // Child thread needs to be created
    debug(Debug::Threads, "Creating std::thread for %d", Args->L);
    Args->SysThread = new std::thread(outerLoop, Args);
    // The Thread object is created by the the new thread itself.
    // And allowed to run asynchronously.
    // Until we set this the new thread cannot execute, so the assignment
    // above should not be a race. The code in outerLoop which looks
    // at Args.SysThread cannot execute before we have assigned the value.
    Args->SysThread->detach();
    // Thread data structure initialization happens at the beginning of outerLoop, since
    // it needs to set the appropriate thread static data, so has to be executed
    // by the thread itself, and allocating the thread data structures inside the thread may
    // improve performance in a NUMA system if they are allocated first touch.
  }
}

Thread::Thread(ThreadArgs const * Args)
    : Team(Args->T), LocalId(Args->L), taskgroup(nullptr), childTasks(0),
      DynamicLoopCount(0), CurrentLoop(0), SinglesSeen(0),
      CurrentReduction(UnknownReduction), GlobalId(Args->G),
      SystemThread(Args->SysThread) {
  // Initialize the thread static variable so we can find ourselves again.
  MyThread = this;
  // And set our place in the team.
  Team->setThread(LocalId, this);

  debug(Debug::Threads, "Created thread %d with system thread %p", LocalId,
        SystemThread);

  // Create and initialize the task pool for this thread.
  taskPool = lomp::Tasking::TaskPoolFactory();

  debug(Debug::Threads, "Thread %d running, about to force affinity", LocalId);

  // Pin thread to core and register it in the NUMA database This
  // should really be done in Thread::outerLoop before we allocate the
  // thread structure, so that if the memory allocator is thread
  // sensitive it can be used. However that would require changes in
  // the implementation of the NUMA code, since it maintains a map
  // from core to Thread::Thread objects, which clearly can't be
  // initialized before the Thread::Thread object is created!  (Of
  // course, this may all be worrying over nothing significant; we
  // haven't measured the impact of allocating these structures in
  // different ways and its actually unlikely to be important compared
  // with other performance mis-features!)
  forceAffinity(LocalId);
  numa::RegisterThread(this, LocalId);

  // We've done enough that we can enter the barrier to say we've been
  // created, and wait for all other threads to have got here too if we're not the master.
  Team->incCreated();
  if (LocalId != 0) {
    debug(Debug::Threads, "Thread %d waiting for all threads to be created.",
          LocalId);
    Team->waitForCreation();
  }
}

void Thread::outerLoop(ThreadArgs const * Args) {
  // It may be better to force the thread affinity here, before allocating space
  // for the new thread, since the aim is to ensure that the thread's data structures
  // are initialized by the thread itself, so that first-touch NUMA allocation
  // sees the appropriate thread make the first touch.
  Thread * Self = new Thread(Args);
  Barrier * b = Self->Team->getBarrier();
  auto me = Self->LocalId;

  debug(Debug::Threads, "Thread %d in outerLoop", me);

  // Wait in a barrier until there is work, execute the work, and loop back to the barrier
  for (;;) {
    // Wait for work
    auto work = b->checkOut(false, me);
    // Run it
    Self->run(work);
    // And tell everyone else we're done before looping around to wait again.
    b->checkIn(me, false);
  }
}

void InvocationInfo::run(void * GTid, void * LTid) const {
  switch (Type) {
  case CallLLVM:
    runLLVM(GTid, LTid);
    break;
#if LOMP_GNU_SUPPORT
  case CallGNU:
    runGNU(GTid, LTid);
    break;
#endif
  default:
    lomp::fatalError("Unknown calling convention in Invocation::run(): %d.",
                     Type);
  }
}

// Invoke the thunk function with LLVM-style calling convention passing it
// the arguments it requires. For complete safety this would require assembler
// code since in the case where we need to push arguments into the stack, we
// must allocate space on the stack and ensure that it is at the current
// bottom of the stack. There is no way guaranteed by the C/C++ standard to
// do that.
//
// However... it seems that implementations of stack variable length arrays [VLA]
// do the "obvious" thing, and allocate space for the VLA by pulling the
// stack down. Therefore the most recently allocated VLA is in the right place!
// Therefore code like this works (at least on X86_64, AARCH{32,64} and RISC-V).
//
// This code is only required because the runtime ABI for this was created a long
// time before C++ lambda functions were standardized. If one were designing it now
// all of this work would be subsumed by the lambda function interfaces which the
// compiler (and C++ runtime) already have to support.
void InvocationInfo::runLLVM(void * GTid, void * LTid) const {
  // Increment the number of executing tasks once, as each thread executes an
  // implicit task for the parallel region.
  auto thread = Thread::getCurrentThread();
  auto team = thread->getTeam();
  ++team->activeTasks;

  // Create our own, private, copy of the va_list describing the arguments so that we can
  // safely unpack them without races.
  va_list VACopy;
  va_copy(VACopy, *ArgsLLVM);

  // Extract the register arguments. We have to do this because the order in which
  // arguments to a function are evaluated is not specified, so if we wrote
  //   Body (va_arg(VACopy, (void *)), va_arg (VACopy, (void *)));
  // the arguments could be reversed.
  // (In general, any permutation of the arguments could be generated!)
  void * RegisterArgs[MAX_REGISTER_ARGS];
  int NumRegArgs = std::min(MAX_REGISTER_ARGS - 2, ArgCount);

  // First two arguments are fixed, not from those passed by the ellipsis in the fork call.
  RegisterArgs[0] = GTid;
  RegisterArgs[1] = LTid;
  // Others are the ones passed in the va_list.
  for (auto i = 0; i < NumRegArgs; i++) {
    RegisterArgs[i + 2] = va_arg(VACopy, void *);
  }

  int StackArgCount = ArgCount - NumRegArgs;
  // Here is where we're exploiting implementation behavior
  // which is not mandated by the language standard. We're
  // relying on the fact that many compilers implement the
  // Variable Length Array [VLA] by pulling the stack-pointer
  // down, which means that the base of the array is pointed to
  // by the stack-pointer.
  // There may also be C++ standard issues with VLA (or alloca), since
  // they are not stanard C++, however, we *know* that this is all a hack!
  // Since the calling convention (at least on Arm, RISC-V and
  // X86_64) requires that additional arguments beyond those in
  // registers are passed on the stack, our VLA is then in just
  // the right place so that when the call is made the called
  // code looks there for the extra arguments!
  // A VLA must always be allocated with a size >= 1, so we
  // ensure that happens even if we don't actually need this array at all.
  void * StackArgs[std::max(StackArgCount, 1)];
  // Copy the stack arguments if there are any.
  for (int i = 0; i < StackArgCount; i++)
    StackArgs[i] = va_arg(VACopy, void *);

  // Since the compiler can't see that the stack arguments matter we
  // have to ensure that it doesn't eliminate them completely!
  // Note that although this is assembly code, it is entirely portable
  // across architectures since it doesn't generate any instructions,
  // (or, at most, a mov from the stack-pointer to a general register, which should be fast!).
  // It just ensures that the compiler knows that the code does look at the StackArgs array.
  __asm__ volatile("# Ensure compiler doesn't eliminate our VLA since the "
                   "compiler doesn't know it's used by the called code"
                   :
                   : "r"(StackArgs));
  // Another alternative here would be
  // (void) * (void * volatile *)&StackArgs[0];
  // Though that will force the generation of a load operation, whereas
  // the asm code doesn't, it just tells the compiler that the array is
  // being accessed.

  // Finally apply the function. Note that we don't have to do
  // anything visible to pass the the stack arguments since they
  // are accessed implicitly via the incoming stack pointer,
  // which is why we needed the trickery above to ensure that
  // the compiler didn't eliminate the StackArgs array and all
  // the code to initialize it!
#if (0)
  if (thread->getLocalId() == 0) {
    errPrintf("Making call:\n  Register args ");
    for (auto i = 0; i < MAX_REGISTER_ARGS; i++) {
      errPrintf("%p ", RegisterArgs[i]);
    }
    errPrintf("\n  Stack args    ");
    for (auto i = 0; i < StackArgCount; i++)
      errPrintf("%p ", StackArgs[i]);
    errPrintf("\n");
  }
#endif

  // We always pass all of the register arguments, whether we need
  // them or not.  That is simpler to write than passing only the used
  // arguments, and is likely just as fast, since we'd be trading
  // some, likely badly predicted, branches for a few loads from the
  // stack from data we just wrote which will be hot in our cache.
  // This also means that we don't need to express the type as a
  // variadic function. That is good because on aarch64 Apple seem to
  // use a different calling convention for variadic functions from
  // the one used for normal functions, with the result that this code
  // broke, as the outlined function was expecting to receive
  // arguments in the normal way, but was passed them in the variadic
  // way.
#if (MAX_REGISTER_ARGS == 6)
  // Six arguments are passed in registers
  BodyLLVM(RegisterArgs[0], RegisterArgs[1], RegisterArgs[2], RegisterArgs[3],
           RegisterArgs[4], RegisterArgs[5]);
#elif (MAX_REGISTER_ARGS == 8)
  // Eight arguments are passed in registers
  // Others are the ones passed on the stack.
  BodyLLVM(RegisterArgs[0], RegisterArgs[1], RegisterArgs[2], RegisterArgs[3],
           RegisterArgs[4], RegisterArgs[5], RegisterArgs[6], RegisterArgs[7]);
#else
#error "Need code for a MAX_REGISTER_ARGS value which is neither 6 nor 8"
#endif
  // Doesn't seem to generate any code (maybe a nop on X86_64 for some
  // reason!), but the standard requires that we do it. So we do, even
  // though this code is explicitly not standard conforming!
  //
  va_end(VACopy);

  --team->activeTasks;
}

#if (LOMP_GNU_SUPPORT)
// Invoke the thunk function with GNU-style calling convention.  This is a bit
// easier than LLVM, as here the thunk arguments are passed as a memory area
// that is passed through as a pointer argument. So, we need not worry about
// the actual ABI and how many arguments are passed in registers or passed as
// in-memory arguments.
void InvocationInfo::runGNU(void *, // GTid
                            void *  // LTid
) const {
  // Increment the number of executing tasks once, as each thread executes an
  // implicit task for the parallel region.
  auto thread = Thread::getCurrentThread();
  auto team = thread->getTeam();
  ++team->activeTasks;

  BodyGNU(ArgsGNU);

  --team->activeTasks;
}
#endif

bool Barrier::checkIn(int me, bool internal) {
  // Handle tasking interface code which the barrier implementations
  // don't want to know about.
  Tasking::TaskExecutionBarrier(internal);
  // Then invoke the virtual method to call whichever implementation we're using.
  return checkIn(me);
}

// Fixed tree of fan in 16, using flags, with a flat broadcast where
// four threads poll each cache line.
// One could certainly tune this for different architectures and/or scales
// of machine!
#define DEFAULT_BARRIER "FT16FlagLBW4"

Barrier * Barrier::newBarrier(int NumThreads) {
  static barrierFactory createBarrier = nullptr;

  if (UNLIKELY(createBarrier == nullptr)) {
    std::string barrierName;
    bool envPresent = environment::getString("LOMP_BARRIER_KIND", barrierName,
                                             DEFAULT_BARRIER);
    auto barrierDesc = Barrier::findBarrier(barrierName);

    if (!barrierDesc) {
      errPrintf("LOMP: Cannot find barrier '%s'!\n", barrierName.c_str());
      Barrier::printBarriers();
      fatalError("Need a barrier!");
    }
    if (envPresent) {
      errPrintf("LOMP: Using user selected barrier %s [%s]\n",
                barrierDesc->name, barrierDesc->getFullName());
    }
    debug(Debug::Info, "Using barrier %s [%s]", barrierDesc->name,
          barrierDesc->getFullName());
    createBarrier = barrierDesc->factory;
  }
  return createBarrier(NumThreads);
}

// Minimal reduction support.
std::pair<char const *, Thread::ReductionType> Thread::reductionNames[] = {
    {"atomic", AtomicReduction},
    {"critical", CriticalSectionReduction},
    {"tree", TreeReduction}};

// Mostly for testing, allow the user to force a reduction type
Thread::ReductionType Thread::forcedReduction = UnknownReduction;

char const * Thread::reductionName(ReductionType r) {
  for (auto & [name, type] : reductionNames) {
    if (type == r)
      return name;
  }
  return "unknown";
}

void Thread::initializeForcedReduction() {
  std::string requestedName;

  if (environment::getString("LOMP_REDUCTION_STYLE", requestedName, "none")) {
    for (auto & [name, type] : reductionNames) {
      if (name == requestedName) {
        forcedReduction = type;
        debug(Debug::Info, "LOMP_REDUCTION_STYLE forced reduction type '%s'",
              requestedName.c_str());
        return;
      }
    }
    printWarning("Unknown reduction (LOMP_REDUCTION_STYLE='%s') requested. "
                 "Using default (i.e., allowing compiled code to choose).",
                 requestedName.c_str());
  }
}

// Choose the reduction type to use based on the available ones and that requested by the user.
Thread::ReductionType Thread::chooseReduction(int32_t flags) {
  static std::atomic<bool> warned = false;

  switch (forcedReduction) {
  case AtomicReduction:
    // We were asked to do an atomic, check that the compiler generated that code.
    if (flags & KMP_IDENT_ATOMIC_REDUCE) {
      // It did, so all is well.
      return AtomicReduction;
    }
    break;
  case CriticalSectionReduction:
    // We were asked to do a critical section; the compiler is supposed always to allow that!
    return CriticalSectionReduction;
  case UnknownReduction:
    // The user didn't force anything, so we can choose.
    // If the compiler generated it, do an atomic reduction, if not,
    // use a critical section.
    return (flags & KMP_IDENT_ATOMIC_REDUCE) ? AtomicReduction
                                             : CriticalSectionReduction;
  default:
    break;
  }
  // We don't yet support anything else, so complain (but only once!)
  bool old = false;
  if (warned.compare_exchange_strong(old, true)) {
    printWarning("Cannot use requested reduction '%s', using 'critical'",
                 reductionName(forcedReduction));
  }
  return CriticalSectionReduction;
}
// This relies on the compiler, and does not support tree reduction in a barrier, which
// it clearly should.
int32_t Thread::enterReduction(ident_t * id, void * lock) {
  auto flags = id->flags;
  auto reductionType = chooseReduction(flags);

  setReduction(reductionType);

  switch (reductionType) {
  case AtomicReduction:
    // The compiler has generated code that will use an atomic for the reduction.
    // We tell it to use that.
    debug(Debug::Reduction, "Entering reduction using atomic reduction");
    return 2; // Requests the compiled code to use the atomic technique.

  case CriticalSectionReduction:
    debug(Debug::Reduction, "Entering reduction using critical section");
    locks::EnterCritical(static_cast<omp_lock_t *>(lock));
    return 1; // Requests the compiled code to use the code that expects to be inside a critical section.

  case TreeReduction:
    fatalError("Tried to use tree reduction which isn't yet implemented!");

  default:
    fatalError("No suitable reduction implementation is available at %s",
               id->psource);
  }
}

void Thread::leaveReduction(void * lock, bool needBarrier) {
  auto reductionType = getReduction();
  auto me = getLocalId();

  debug(Debug::Reduction, "%d: Leaving %s reduction", me,
        reductionName(reductionType));
  if (reductionType == CriticalSectionReduction) {
    locks::LeaveCritical(static_cast<omp_lock_t *>(lock));
  }
  if (needBarrier) {
    // And then wait at the barrier if required.
    debug(Debug::Reduction, "%d Barrier at the end of the reduction", me);
    Team->getBarrier()->fullBarrier(me);
  }
}
} // namespace lomp
