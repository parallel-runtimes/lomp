//===-- locks.cc - Implement OpenMP locks -----------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of OpenMP locks.
/// Move the implementations from microBM/locks.cc into here, and expand slightly.
/// (Need the recursive locks, for instance).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <pthread.h>
#include "omp.h"
#include "target.h"
#include "locks.h"
#include "globals.h"
#include "threads.h"
#include "environment.h"
#include "stats-timing.h"
#include "mlfsr32.h"

namespace lomp::locks {

// For the moment we always have a pointer in an omp_lock_t to an
// object derived from our abstract base class, and use new/delete
// (which are thread safe) to allocate these objects.  For some locks
// this may have a performance overhead, but it is simple!
//
// A base class so that we can have a consistent interface This does
// mean that we're doing an indirect call for each operation, but it
// probably doesn't matter.
//
// Note that this meets the requirements of the C++ "Lockable"
// https://en.cppreference.com/w/cpp/named_req/Lockable ;
// therefore it can be used behind std::lock_guard if required.
class abstractLock {
public:
  abstractLock() {}
  virtual ~abstractLock() {}
  virtual bool try_lock() = 0;
  virtual void lock() = 0;
  virtual void unlock() = 0;
  virtual char const * name() const = 0;
};

// Locks we may use.
// pthread mutex
#include <pthread.h>
class pthreadMutexLock : public abstractLock {
  pthread_mutex_t mutex;

public:
  pthreadMutexLock() {
    pthread_mutex_init(&mutex, 0);
  }
  ~pthreadMutexLock() {
    pthread_mutex_destroy(&mutex);
  }
  bool try_lock() {
    return pthread_mutex_trylock(&mutex);
  }
  void lock() {
    pthread_mutex_lock(&mutex);
  }
  void unlock() {
    pthread_mutex_unlock(&mutex);
  }
  char const * name() const {
    return "pthread_mutex_lock(default)";
  }
  static abstractLock * create() {
    return new pthreadMutexLock();
  }
};

class cxxMutexLock : public abstractLock {
  std::mutex theLock;

public:
  cxxMutexLock() {}
  ~cxxMutexLock() {}
  bool try_lock() {
    return theLock.try_lock();
  }
  void lock() {
    theLock.lock();
  }
  void unlock() {
    theLock.unlock();
  }
  char const * name() const {
    return "std::mutex";
  }
  static abstractLock * create() {
    return new cxxMutexLock();
  }
};

// A Test and test&set lock with no backoff.
// Used here as the base of the speculative lock.
class TTASLock : public abstractLock {
  CACHE_ALIGNED std::atomic<bool> locked;

public:
  TTASLock() : locked(false) {}
  ~TTASLock() {}
  bool try_lock() {
    bool expected = false;
    // Try an atomic before anything else so that we don't move the line
    // into a shared state then immediately to exclusive if it is unlocked
    return locked.compare_exchange_strong(expected, true,
                                          std::memory_order_acquire);
  }
  void lock() {
    for (;;) {
      if (try_lock())
        return;
      // But if we see it locked, just look at it until it is unlocked.
      // Here we're polling something in our cache.
      while (locked) {
        Target::Yield();
      }
    }
  }
  void unlock() {
    locked.store(false, std::memory_order_release);
  }
  bool isLocked() const {
    return locked;
  }
  char const * name() const {
    return "Test and Test&Set";
  }
  static abstractLock * create(uint64_t) {
    return new TTASLock();
  }
};

// A Test and test&set lock with backoff
class TTASLockBO : public TTASLock {
public:
  TTASLockBO() {}
  ~TTASLockBO() {}
  void lock() {
    for (;;) {
      if (try_lock())
        return;
      // But if we see it locked, wait a while before trying again.
      // Here we're polling something in our cache, so the main effect
      // of backoff is to increase clumping. (We could also
      // evict the lock from our cache, which might improve
      // lock transfer time).
      randomExponentialBackoff backoff;
      while (isLocked()) {
        backoff.sleep();
      }
    }
  }

  char const * name() const {
    return "TTAS e**x backoff";
  }
  static abstractLock * create() {
    return new TTASLockBO();
  }
};

// A Mellor-Crummey, Scott queueing lock.
class MCSLock : public abstractLock {
  enum { MAX_THREADS = 256 };
  class MCSLockEntry {
  public:
    CACHE_ALIGNED std::atomic<MCSLockEntry *> next;
    std::atomic<bool> go;

    MCSLockEntry() : next(0), go(false) {}
    ~MCSLockEntry() {}
  };

  CACHE_ALIGNED std::atomic<MCSLockEntry *> tail;
  MCSLockEntry entries[MAX_THREADS];

public:
  MCSLock() : tail(0) {}
  ~MCSLock() {}
  static int maxThreads() {
    return MAX_THREADS;
  }

  bool isLocked() const {
    return tail != 0;
  }

  bool try_lock() {
    // Test before test&set...
    if (isLocked())
      return false;

    // Now cmpxchg ourselves in, atomically checking the lock
    // is still free.
    int myId = Thread::getCurrentThread()->getGlobalId();
    MCSLockEntry * me = &entries[myId];
    MCSLockEntry * empty = 0;

    return tail.compare_exchange_strong(empty, me, std::memory_order_acquire);
  }

  void lock() {
    int myId = Thread::getCurrentThread()->getGlobalId();
    MCSLockEntry * me = &entries[myId];
    MCSLockEntry * t = tail.exchange(me, std::memory_order_acquire);

    // No one in the queue, so I own the lock.
    if (t == 0)
      return;
    // Link ourself into the previous tail.
    t->next = me;

    // And wait to be woken
    while (!me->go) {
      Target::Yield();
    }
  }

  void unlock() {
    int myId = Thread::getCurrentThread()->getGlobalId();
    MCSLockEntry * me = &entries[myId];
    // Need a sacrificial copy, since compare_exchange always switches in the
    // value it reads.
    MCSLockEntry * expected = me;
    // Am I still the tail? If so no-one else is waiting.
    if (!tail.compare_exchange_strong(expected, 0, std::memory_order_release)) {
      // Someone is waiting, but they may not yet have updated the pointer
      // in our entry, even though they've swapped in the global tail pointer,
      // so we may have to wait until they have.
      MCSLockEntry * nextInLine = me->next;
      for (; nextInLine == 0; nextInLine = me->next) {
        Target::Yield();
      }
      // Now we know who they are we can release them.
      nextInLine->go = true;
    }
    // Reset our state ready for the next acquire.
    me->go = false;
    me->next = 0;
  }
  char const * name() const {
    return "MCS";
  }
  static abstractLock * create() {
    // Check that we don't have too many threads. This would be more complicated
    // if we allowed nesting and dynamic creation of more threads.
    // Note that for this to be called the environment must have been parsed,
    // which happens at the same time as thread team creation.
    if (lomp::Thread::getCurrentThread()->getTeam()->getCount() > MAX_THREADS) {
      fatalError("Too many threads (>%d) in MCS lock...", MAX_THREADS);
    }

    return new MCSLock();
  }
};

#if LOMP_HAVE_RTM
// A template to add speculation to an existing base lock.
// The base lock must implement an "isLocked() const" method,
// which should ensure that the cache-line which holds
// the lock status (and which will be modified on acquire or release)
// is read. (It's hard to see how it can tell without doing that!)
template <class baseLockType>
class speculativeLock : public abstractLock {
  baseLockType baseLock;
  std::string lockName;

public:
  speculativeLock() {
    if (!Target::HaveSpeculation()) {
      fprintf(stderr, "Hardware speculation is not enabled on this machine\n");
      exit(1);
    }
    lockName = "Speculative " + std::string(baseLock.name());
  }
  ~speculativeLock() {}

  bool try_lock() {
    // Try to speculate
    if (Target::StartSpeculation() == -1) {
      // Executing speculatively.

      // Now check that the lock is free.
      if (baseLock.isLocked()) {
        // Executing this causes the Target::StartSpeculation
        // call above to return again!
        // We can't have Target:: on this because it has to be a macro.
        Target_AbortSpeculation(0);
        // Execution cannot get to this point.
      }
      // N.B. executing baseLock.isLocked() above ensures that it is
      // in our read-set, so that if the lock is taken we will abort.
      // We're now inside the transaction and can let the user code run.
      return true;
    }
    else {
      return false;
    }
  }

  void lock() {
    while (baseLock.isLocked()) {
      // Wait for the base lock to be unlocked.
      // This is unpleasant and makes the use of a queueing lock
      // futile, since under contention many threads will be polling here.
      // We need to do this to avoid the latch effect which means that when
      // speculation fails you cannot return to speculation until no one is
      // waiting for the lock. With this you can resume speculation as soon
      // as the lock is freed.
      Target::Yield();
    }

    if (!try_lock()) {
      // Either we aborted, or the lock was taken for real. In either case
      // we try to take it for real.
      baseLock.lock();
    }
  }

  void unlock() {
    // If we are executing transactionally, we can never see the
    // baseLock in its locked state, (because we wouldn't have started
    // transactional execution if the lock was locked, and, if someone
    // claimed it after we started transactional execution we would
    // have been aborted because of the write after read conflict).
    // We can therefore check the lock state to determine how to
    // release it.
    if (baseLock.isLocked())
      baseLock.unlock();
    else
      Target::CommitSpeculation();
  }
  char const * name() const {
    return lockName.c_str();
  }
  static abstractLock * create() {
    return new speculativeLock();
  }
};
#endif

// The function used to construct OpenMP locks
// This may be changed under control of an envirable.

static abstractLock * (*lockFactory)() = cxxMutexLock::create;
static bool locksInitialized = false;

// Locks which can be selected by envirable
static struct {
  char const * name;
  abstractLock * (*factory)();
} lockTags[] = {
    {"TTAS", TTASLockBO::create},
    {"MCS", MCSLock::create},
    {"cxx", cxxMutexLock::create},
    {"pthread", pthreadMutexLock::create},
#if LOMP_LOCK_RTM
    {"speculative", speculativeLock<TTASLock>::create},
#endif
};

void InitializeLocks() {
  if (locksInitialized)
    return;
  locksInitialized = true;
  std::string lockKind;
  int const numLocks = sizeof(lockTags) / sizeof(lockTags[0]);

  if (environment::getString("LOMP_LOCK_KIND", lockKind, "")) {
    // errPrintf("Looking for lock %s\n",lockKind.c_str());
    for (int i = 0; i < numLocks; i++) {
      if (lockTags[i].name == lockKind) {
        lockFactory = lockTags[i].factory;
#if (0)
        {
          abstractLock * l = lockFactory();
          errPrintf("*** Using lock %s\n", l->name());
          delete l;
        }
#endif
        return;
      }
    }
    errPrintf(
        "***WARNING*** LOMP_LOCK_KIND=%s ignored, %s is not a valid lock\n"
        "Valid lock names are ",
        lockKind.c_str(), lockKind.c_str());
    for (int i = 0; i < numLocks - 1; i++) {
      errPrintf("%s,", lockTags[i].name);
    }
    errPrintf("%s\n", lockTags[numLocks - 1].name);
  }
}

// The user interface functions.
void InitLock(omp_lock_t * lock) {
  lock->_lk = lockFactory();
}

void DestroyLock(omp_lock_t * lock) {
  delete static_cast<abstractLock *>(lock->_lk);
}

void SetLock(omp_lock_t * lock) {
  static_cast<abstractLock *>(lock->_lk)->lock();
}

void UnsetLock(omp_lock_t * lock) {
  static_cast<abstractLock *>(lock->_lk)->unlock();
}

int TestLock(omp_lock_t * lock) {
  return static_cast<abstractLock *>(lock->_lk)->try_lock();
}

std::mutex MutexCriticalInit;
void EnterCritical(omp_lock_t * lock) {
  if (!lock->_lk) {
    std::lock_guard<std::mutex> guard(MutexCriticalInit);
    if (!lock->_lk) {
      // The lock has not yet been initialized, so let's do that now
      InitLock(lock);
    }
  }
  SetLock(lock);
}

void LeaveCritical(omp_lock_t * lock) {
  UnsetLock(lock);
}

} // namespace lomp::locks
