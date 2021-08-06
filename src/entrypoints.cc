//===-- entrypoints.cc - Interface functions into the runtime ---*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains functions which have C binding and are called directly by
/// the compiler or user.
///
//===----------------------------------------------------------------------===//
#include "debug.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "interface.h"
#include "threads.h"
#include "globals.h"
#include "tasking.h"
#include "locks.h"

extern "C" {

// Functions called by the user code directly.
int32_t omp_get_thread_num(void) {
  lomp::Thread * Me = lomp::Thread::getCurrentThread();

  return Me->getTeam()->inParallel() ? Me->getLocalId() : 0;
}

int32_t omp_get_num_threads(void) {
  lomp::ThreadTeam * Team = lomp::Thread::getCurrentThread()->getTeam();

  return Team->inParallel() ? Team->getCount() : 1;
}

void omp_set_num_threads(int nthreads) {
  // Changing the number of threads is not yet supported, so we abort if that is attempted.
  if (lomp::NumThreads != 0 && lomp::NumThreads != nthreads) {
    lomp::fatalError("Altering the number of threads is not implemented. "
                     "(Changing from %d to %d)",
                     lomp::NumThreads, nthreads);
  }
  lomp::NumThreads = nthreads;
}

// May not be right if we ever support nesting...
int32_t omp_get_max_threads(void) {
  return lomp::Thread::getCurrentThread()->getTeam()->getCount();
}

int32_t omp_in_parallel(void) {
  return lomp::Thread::getCurrentThread()->getTeam()->inParallel();
}

void omp_set_schedule(omp_sched_t schedule, int chunk) {
  lomp::setScheduleInfo(schedule, chunk);
}

void omp_get_schedule(omp_sched_t * schedp, int * chunkp) {
  lomp::getScheduleInfo(schedp, chunkp);
}

void omp_init_lock(omp_lock_t * lock) {
  lomp::locks::InitLock(lock);
}

void omp_init_lock_with_hint(omp_lock_t * lock,
                             [[maybe_unused]] omp_lock_hint_t hint) {
  // We deliberately don't look at the hint, but rather map the locks
  // with hints to the standard lock implementation.
  omp_init_lock(lock);
}

void omp_destroy_lock(omp_lock_t * lock) {
  lomp::locks::DestroyLock(lock);
}

void omp_set_lock(omp_lock_t * lock) {
  lomp::locks::SetLock(lock);
}

void omp_unset_lock(omp_lock_t * lock) {
  lomp::locks::UnsetLock(lock);
}

int omp_test_lock(omp_lock_t * lock) {
  return lomp::locks::TestLock(lock);
}

double omp_get_wtime(void) {
  double time = lomp::getTime();
  return time;
}

// Functions called by the compiler itself.
void __kmpc_push_num_threads(ident_t *, int32_t, int32_t nthreads) {
  debug_enter();
  // For now just call omp_set_num_threads; if/when we allow changing the number of threads,
  // nested parallelism and so on this may need to change too.
  omp_set_num_threads(nthreads);
  debug_leave();
}

void __kmpc_fork_call(ident_t * loc, int32_t argc, lomp::BodyTypeLLVM thunk,
                      ...) {
  debug_enter();

  // getCurrentThread will initialize the runtime if need be, so we don't
  // need to check explicitly here.

  // Set up the va_list so that we can pull the arguments
  va_list thunk_args;
  va_start(thunk_args, thunk);

  // Build the packedBody so that we can pass the arguments around
  lomp::InvocationInfo packedBody(thunk, argc, &thunk_args);

  // Find the thread team and associated data structures
  lomp::Thread * Me = lomp::Thread::getCurrentThread();
  lomp::ThreadTeam * Team = Me->getTeam();
  lomp::Barrier * Barrier = Team->getBarrier();

  // For now we don't support nested parallelism, so abort if it is attempted.
  if (UNLIKELY(Team->inParallel())) {
    lomp::fatalError("Nested parallelism is not supported: attempted from %s",
                     loc->psource);
  }

  // Remember that we're now inside a parallel region
  Team->enterParallel();

  // Dispatch work to the other threads
  LOMP_ASSERT(Me->getLocalId() == 0);
  Barrier->wakeUp(0, &packedBody);

  // Execute the body here too.
  Me->run(&packedBody);

  // Wait for all threads to complete the work.
  Barrier->checkIn(0, false);

  // Clean up; the va_args are referenced from each thread, so we can't destroy them before this...
  // It may be worth investigating whether that is optimal, or whether flattening the arguments and
  // broadcasting the flattened data is preferable. (That is what the LLVM runtime does).
  va_end(thunk_args);
  Team->leaveParallel();

  debug_leave();
}

// Perform a full barrier with no reduction.
void __kmpc_barrier(ident_t *, // where
                    int32_t) { // gtid
  // Find the thread team and associated data structures
  auto Me = lomp::Thread::getCurrentThread();
  auto Barrier = Me->getTeam()->getBarrier();

  // Let the barrier do its stuff!
  Barrier->fullBarrier(Me->getLocalId());
}

int32_t __kmpc_global_thread_num(ident_t *) { //where
  // Prevent calling this from initializing the runtime if it's not already initialized.
  if (UNLIKELY(!lomp::RuntimeInitialized)) {
    return 0;
  }
  return lomp::Thread::getCurrentThread()->getGlobalId();
}

int32_t __kmpc_in_parallel(ident_t *) {
  return omp_in_parallel();
}

int32_t __kmpc_reduce_nowait(
    ident_t * id, int32_t, // where and gtid
    int32_t /* num_vars */, size_t /* red_size */, void * /* red_data*/,
    void (*/* red_func*/)(void * lhs_data, void * rhs_data), void * lck) {
  // N.B. We ought to pass in more arguments and stash them, but for now
  // we're not supporting tree reductions, so don't need to.
  return lomp::Thread::getCurrentThread()->enterReduction(id, lck);
}

void __kmpc_end_reduce_nowait(ident_t *, int32_t, // where and gtid
                              void * lck) {
  lomp::Thread::getCurrentThread()->leaveReduction(lck, false);
}

int32_t __kmpc_reduce(ident_t * id, int32_t, // where and gtid
                      int32_t /* num_vars */, size_t /* red_size */,
                      void * /* red_data*/,
                      void (*/* red_func*/)(void * lhs_data, void * rhs_data),
                      void * lck) {
  // N.B. We ought to pass in more arguments and stash them, but for now
  // we're not supporting tree reductions, so don't need to.
  return lomp::Thread::getCurrentThread()->enterReduction(id, lck);
}

void __kmpc_end_reduce(ident_t *, // where
                       int32_t,   // gtid
                       void * lck) {
  lomp::Thread::getCurrentThread()->leaveReduction(lck, true);
}

// The flush directive.
// As of January 2021, LLVM does not accept the OpenMP 5.1 addition of a memory
// model, so we always do a full seq_cst flush.
void __kmpc_flush(ident_t *) {
  // Ensure all stores are visible and no loads move above here.
  // See https://en.cppreference.com/w/cpp/atomic/memory_order for more details, or the OpenMP
  // standard's description at https://www.openmp.org/spec-html/5.1/openmpsu106.html#x139-1490002.19.8
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

//
// Loop interfaces are in loops.cc; that avoids having a big template in a header file.
//

// Tasking interfaces
void * __kmpc_omp_task_alloc(ident_t *, // where
                             int32_t,   // gtid
                             void *,    // flags
                             size_t sizeOfTaskClosure, size_t sizeOfShareds,
                             void * thunkPtr) {
  auto thunk = reinterpret_cast<lomp::Tasking::ThunkPointer>(thunkPtr);
  // we just pass the final size and the routine pointer to the allocation routine
  lomp::Tasking::TaskDescriptor * task =
      lomp::Tasking::AllocateTask(sizeOfTaskClosure, sizeOfShareds);
  lomp::Tasking::InitializeTaskDescriptor(task, sizeOfTaskClosure,
                                          sizeOfShareds, thunk);
  return reinterpret_cast<void *>(lomp::Tasking::TaskToClosure(task));
}

int32_t __kmpc_omp_task(ident_t *, // where
                        int32_t,   // gtid
                        void * new_task) {
  auto closure =
      reinterpret_cast<lomp::Tasking::TaskDescriptor::Closure *>(new_task);
  lomp::Tasking::TaskDescriptor * task = lomp::Tasking::ClosureToTask(closure);
  lomp::Tasking::PrepareTask(task);
  lomp::Tasking::StoreTask(task);
  return 0; // Caller ignores return value
}

void __kmpc_omp_task_begin_if0(ident_t *, // where
                               int32_t,   // gtid
                               void *) {  // new_task 
  // Do nothing, as the task is invoked in the compiler-generated code.
}

void __kmpc_omp_task_complete_if0(ident_t *, // where
                                  int32_t,   // gtid
                                  void * new_task) {
  // The invoked task in the compiler-generated code has finished execution,
  // so we need to cleanup the task descriptor here.
  auto closure =
      reinterpret_cast<lomp::Tasking::TaskDescriptor::Closure *>(new_task);
  lomp::Tasking::TaskDescriptor * task = lomp::Tasking::ClosureToTask(closure);
  // TODO: do we need to call PrepareTask here, too?
  lomp::Tasking::CompleteTask(task);
  lomp::Tasking::FreeTaskAndAncestors(task);
}

int32_t __kmpc_omp_taskwait(ident_t *, // where
                            int32_t)   // gtid
{
  lomp::Tasking::TaskWait();
  return 0; // Caller ignores return value
}

int32_t __kmpc_taskgroup(ident_t *, // where
                         int32_t) { // gtid
  lomp::Tasking::TaskgroupBegin();
  return 0;
}

int32_t __kmpc_end_taskgroup(ident_t *, // where
                             int32_t) { // gtid
  lomp::Tasking::TaskgroupEnd();
  return 0;
}

// Serialization: single and master constructs
//
// Handle entering a "single" construct. The thread should only
// execute it if it is the first to get to this dynamic instance.
//
int32_t __kmpc_single(ident_t *,   // where
                      int32_t *) { // gtid
  auto myThread = lomp::Thread::getCurrentThread();
  auto myTeam = myThread->getTeam();
  auto mySingleCount = myThread->fetchAndIncrSingleCount();

  return myTeam->tryIncrementNextSingle(mySingleCount);
}

void __kmpc_end_single(ident_t *, // where
                       int32_t) { // gtid
  // No need to do anything here.
  // (Though OMPT would want to see it!)
}

int32_t __kmpc_master(ident_t *, // where
                      int32_t)   // gtid
{
  return omp_get_thread_num() == 0;
}

void __kmpc_end_master(ident_t *, // where
                       int32_t)   // gtid
{
  // this is a no-op function
  // (Though OMPT would want to see it!)
}

void __kmpc_critical(ident_t *, // where
                     int32_t,   // gtid
                     void * ptr) {
  omp_lock_t * lock = (omp_lock_t *)ptr;
  lomp::locks::EnterCritical(lock);
}

void __kmpc_critical_with_hint(ident_t *, // where
                               int32_t,   // gtid
                               void * ptr, [[maybe_unused]] uint32_t hint) {
  // Ignore the hint and use a standard lock
  __kmpc_critical(nullptr, 0, ptr);
}

void __kmpc_end_critical(ident_t *, // where
                         int32_t,   // gtid
                         void * ptr) {
  omp_lock_t * lock = (omp_lock_t *)ptr;
  lomp::locks::LeaveCritical(lock);
}

#if (LOMP_GNU_SUPPORT)
void GOMP_parallel(void (*thunk)(void *), void * args, unsigned nthreads) {
  debug_enter();

  // Find the thread team and associated data structures
  lomp::Thread * Me = lomp::Thread::getCurrentThread();
  lomp::ThreadTeam * Team = Me->getTeam();

  // For now we don't support nested parallelism, so abort if it is attempted.
  if (UNLIKELY(Team->inParallel())) {
    lomp::fatalError("Nested parallelism is not yet supported: attempted from a call to "
                     "GOMP_parallel.");
  }

  // Similarly changing team size is not yet supported.
  if (nthreads != 0 && Team->getCount() != nthreads) {
    lomp::fatalError("Adjusting team size is not yet supported: a call to "
                     "GOMP_parallel asked for %u threads but the current team"
                     " size is %u.", nthreads, Team->getCount());
  }
  
  // Remember that we're now inside a parallel region
  Team->enterParallel();

  // Dispatch work to the other threads
  LOMP_ASSERT(Me->getLocalId() == 0);
  // Build the packedBody so that we can pass the arguments around
  lomp::InvocationInfo packedBody(thunk, args);
  lomp::Barrier * Barrier = Team->getBarrier();
  Barrier->wakeUp(0, &packedBody);

  // Execute the body here too.
  Me->run(&packedBody);

  // Wait for all threads to complete the work.
  Barrier->checkIn(0, false);

  Team->leaveParallel();

  debug_leave();
}

void GOMP_barrier(void) {
  debug_enter();
  // We can simply map the GNU entry point to the LLVM entry point.
  __kmpc_barrier(nullptr, 0);
  debug_leave();
}

void GOMP_task(void (*thunk)(void *), void * data,
               void (*copyfunc)(void *, void *), long argsz, long argaln,
               bool cond, unsigned flags, void ** dependences) {
  debug_enter();

  if (copyfunc) {
    lomp::printWarning(
        "The GOMP_task entrypoint does not support copy functors.");
  }

  printf("flags=%d, cond=%d\n", flags, cond);

  // Use the LLVM-style task allocator to create some memory for the task and
  // its descriptor.  To avoid some code duplication, we are faking the thunk
  // pointer by casting it to an LLVM-style thunk pointer (which does not make a
  // real difference when initializing the task).
  auto closure = reinterpret_cast<lomp::Tasking::TaskDescriptor::Closure *>(
      __kmpc_omp_task_alloc(nullptr, 0, nullptr,
                            sizeof(lomp::Tasking::TaskDescriptor), argsz,
                            reinterpret_cast<void *>(thunk)));

  // Memorize that we have a GNU-style thunk function (this overwrites the
  // default that the task initialization has put into the task descriptor).
  closure->thunkType = lomp::Tasking::TaskDescriptor::ThunkType::GNUStyle;

  // Store the task's data pointer in the storage of the task descriptor
  memcpy(closure->data, data, argsz);

  if (cond) {
    // Submit the task for execution using the LLVM-style task API.
    printf("DEFER!\n");
    __kmpc_omp_task(nullptr, 0, closure);
  }
  else {
    printf("IMMEDIATE!\n");
    // This is an if(0) task, so execute it in place, do not defer it.
    __kmpc_omp_task_begin_if0(nullptr, 0, closure);
    auto task = ClosureToTask(closure);
    lomp::Tasking::PrepareTask(task);
    lomp::Tasking::InvokeTask(task);
    // __kmpc_omp_task_complete_if0(nullptr, 0, closure);
  }

  debug_leave();
}

void GOMP_taskwait(void) {
  debug_enter();
  lomp::Tasking::TaskWait();
  debug_leave();
}

void GOMP_taskgroup_start(void) {
  debug_enter();
  lomp::Tasking::TaskgroupBegin();
  debug_leave();
}

void GOMP_taskgroup_end(void) {
  debug_enter();
  lomp::Tasking::TaskgroupEnd();
  debug_leave();
}

int GOMP_single_start(void) {
  debug_enter();
  // Map this call to the existing LLVM call.
  return __kmpc_single(nullptr, nullptr);
  debug_leave();
}

void GOMP_loop_end() {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_end() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_loop_maybe_nonmonotonic_runtime_start() {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_start() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_loop_maybe_nonmonotonic_runtime_next() {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_next() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_critical_start(void) {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_next() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_critical_end(void) {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_next() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_critical_name_start(void ** ptr) {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_next() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}

void GOMP_critical_name_end(void ** ptr) {
  debug_enter();
#if (LOMP_WARN_API_STUBS)
#warning "Function GOMP_loop_maybe_nonmonotonic_runtime_next() not implemented"
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
  debug_leave();
}
#endif

#if (LOMP_ICC_SUPPORT)
/// Intel compiler support functions which LLVM doesn't call.
/// We put them under #if because we can optimize elsewhere if
/// we are only supporting LLVM, but those optimizations will silently
/// break on Intel compiled code, so we want to stop that from linking
/// with the LLVM only library.

void ompc_set_num_threads(int32_t num_threads) {
  omp_set_num_threads(num_threads);
}

void __kmpc_begin(ident_t *, int32_t) {
  // No need to do anything.
}

int32_t __kmpc_ok_to_fork(ident_t *) {
  return true;
}

void __kmpc_serialized_parallel(ident_t * where, int32_t) {
#if (LOMP_WARN_API_STUBS)
#warning "Function "__FUNCTION__"() is not implemented yet."
#endif
  lomp::fatalError("The runtime entrypoint %s (at %s:%d) is not implemented.",
                   __FUNCTION__, __FILE__, __LINE__);
}

void __kmpc_end_serialized_parallel(ident_t *, int32_t) {
  // No need to do anything, since we can't get here without passing through
  // __kmpc_serialized_parallel, but we abort if that's called...
}

void __kmpc_end(ident_t *) {
  // We could shutdown the runtime, but there's no real need to do so.
}

void __kmpc_init_lock(ident_t *, int32_t, // where and gtid
                      void ** lck) {
  omp_lock_t * lock = reinterpret_cast<omp_lock_t *>(lck);
  lomp::locks::InitLock(lock);
}

void __kmpc_set_lock(ident_t *, int32_t, // where and gtid
                     void ** lck) {
  omp_lock_t * lock = reinterpret_cast<omp_lock_t *>(lck);
  lomp::locks::SetLock(lock);
}

void __kmpc_unset_lock(ident_t *, int32_t, // where and gtid
                       void ** lck) {
  omp_lock_t * lock = reinterpret_cast<omp_lock_t *>(lck);
  lomp::locks::UnsetLock(lock);
}

void __kmpc_destroy_lock(ident_t *, int32_t, // where and gtid
                         void ** lck) {
  omp_lock_t * lock = reinterpret_cast<omp_lock_t *>(lck);
  lomp::locks::DestroyLock(lock);
}
#endif

} // extern "C"
