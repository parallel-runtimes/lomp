//===-- locks.cc - Measure lock properties -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains micro-benchmarks to measure the properties of locks.

#include <omp.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <unordered_map>

#include "stats-timing.h"
#include "target.h"
#include "mlfsr32.h"

using namespace lomp;

// We use OpenMP to set up and bind threads, but our measurements here are of hardware properties,
// not those of the OpenMP runtime which is being used.

#if (LOMP_TARGET_LINUX)
// Force thread affinity.
// This is a dubious thing to do; it'd be better to use the functionality
// of the OpenMP runtime, but this is simple, at least.
#include <sched.h>
void forceAffinity() {
  int me = omp_get_thread_num();
  cpu_set_t set;

  CPU_ZERO(&set);
  CPU_SET(me, &set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0)
    fprintf(stderr, "Failed to force affinity for thread %d\n", me);
  //  else
  //    fprintf(stderr,"Thread %d bound to cpuid bit %d\n",me,me);
}
#else
void forceAffinity() {}
#endif

// A base class so that we can have a simple interface to our timing operations
// and pass in a specific lock type to use.
// This does mean that we're doing an indirect call for each operation, but
// that is the same for all cases, and should be well predicted and cached.
class abstractLock {
public:
  abstractLock() {}
  virtual ~abstractLock() {}
  virtual void lock() = 0;
  virtual void unlock() = 0;
  virtual char const * name() const = 0;
};

// Locks we want to measure.
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
  void lock() {
    pthread_mutex_lock(&mutex);
  }
  void unlock() {
    pthread_mutex_unlock(&mutex);
  }
  char const * name() const {
    return "pthread_mutex_lock(default)";
  }
  static abstractLock * create(uint64_t) {
    return new pthreadMutexLock();
  }
};

#include <mutex>
class cxxMutexLock : public abstractLock {
  std::mutex theLock;

public:
  cxxMutexLock() {}
  ~cxxMutexLock() {}
  void lock() {
    theLock.lock();
  }
  void unlock() {
    theLock.unlock();
  }
  char const * name() const {
    return "std::mutex";
  }
  static abstractLock * create(uint64_t) {
    return new cxxMutexLock();
  }
};

// OpenMP locks
class openMPLock : public abstractLock {
  omp_lock_t theLock;
  std::string lockName;

  void initName(omp_lock_hint_t hint) {
    static char const * hintNames[] = {"uncontended", "contended",
                                       "nonspeculative", "speculative"};
    lockName = "omp_lock_t(";
    if (hint == omp_lock_hint_none) {
      lockName += "None)";
      return;
    }
    bool seenHint = false;
    for (int i = 0; i < 4; i++) {
      if (hint & (1 << i)) {
        if (seenHint)
          lockName += "|";
        lockName += hintNames[i];
        seenHint = true;
      }
    }
    lockName += ")";
  }

public:
  openMPLock(omp_lock_hint_t hint = omp_lock_hint_none) {
    omp_init_lock_with_hint(&theLock, hint);
    initName(hint);
  }
  ~openMPLock() {
    omp_destroy_lock(&theLock);
  }
  void lock() {
    omp_set_lock(&theLock);
  }
  void unlock() {
    omp_unset_lock(&theLock);
  }
  char const * name() const {
    return lockName.c_str();
  }
  static abstractLock * create(uint64_t hint) {
    return new openMPLock(omp_lock_hint_t(hint));
  }
};

// Aarrggh gcc seems to define the lock hints, and declare omp_init_lock_with_hint
// in <omp.h>, but then not have the function definition in libgomp!
// Try to hack around that
static bool hintsIgnored = false;
#if defined(__GNUC__)
extern "C" {
void omp_init_lock_with_hint(omp_lock_t * lock, omp_lock_hint_t)
    __attribute__((weak));

void omp_init_lock_with_hint(omp_lock_t * lock, omp_lock_hint_t) {
  fprintf(stderr, "# BEWARE Lock hints ignored\n");
  hintsIgnored = true;
  omp_init_lock(lock);
}
}
#endif

// A Test & Set lock
class TASLock : public abstractLock {
  CACHE_ALIGNED std::atomic<bool> locked;

public:
  TASLock() : locked(false) {}
  ~TASLock() {}
  void lock() {
    bool expected = false;
    while (!locked.compare_exchange_weak(expected, true,
                                         std::memory_order_acquire)) {
      expected = false;
      Target::Yield();
    }
  }
  void unlock() {
    locked.store(false, std::memory_order_release);
  }
  char const * name() const {
    return "Test&Set";
  }
  static abstractLock * create(uint64_t) {
    return new TASLock();
  }
};

// A Test and test&set lock
class TTASLock : public abstractLock {
  CACHE_ALIGNED std::atomic<bool> locked;

public:
  TTASLock() : locked(false) {}
  ~TTASLock() {}
  void lock() {
    for (;;) {
      bool expected = false;
      // Try an atomic before anything else so that we don't move the line
      // into a shared state then immediately to exclusive if it is unlocked
      if (locked.compare_exchange_strong(expected, true,
                                         std::memory_order_acquire))
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

// A Test and test&set lock with backoff (we expect this to be futile)
class TTASLockBO : public abstractLock {
  CACHE_ALIGNED std::atomic<bool> locked;

public:
  TTASLockBO() : locked(false) {}
  ~TTASLockBO() {}
  void lock() {
    for (;;) {
      bool expected = false;
      // Try an atomic before anything else so that we don't move the line
      // into a shared state then immediately to exclusive if it is unlocked
      if (locked.compare_exchange_strong(expected, true,
                                         std::memory_order_acquire))
        return;
      // But if we see it locked, just look at it until it is unlocked.
      // Here we're polling something in our cache.
      randomExponentialBackoff backoff;
      while (locked) {
        backoff.sleep();
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
    return "TTAS e**x backoff";
  }
  static abstractLock * create(uint64_t) {
    return new TTASLockBO();
  }
};

// A Test & Set lock based on an atomic exchange operation instead of a CAS
class XCHGLock : public abstractLock {
  CACHE_ALIGNED std::atomic<uint32_t> value;
  // zero => unlocked
  // one  => locked
public:
  XCHGLock() {
    value = 0;
  }
  ~XCHGLock() {}
  void lock() {
    while (value.exchange(1, std::memory_order_acquire) == 1) {
      Target::Yield();
    }
  }
  void unlock() {
    value.store(0, std::memory_order_release);
  }
  char const * name() const {
    return "Xchg";
  }
  static abstractLock * create(uint64_t) {
    return new XCHGLock();
  }
};

// A ticket/bakery lock
class ticketLock : public abstractLock {
  // Place serving and next in different cache lines. Entering the lock and
  // updating next doesn't need to disturb those waiting for serving to
  // update.
  CACHE_ALIGNED std::atomic<uint32_t> serving;
  CACHE_ALIGNED std::atomic<uint32_t> next;

public:
  ticketLock() : serving(0), next(0) {}
  ~ticketLock() {}

  void lock() {
    uint32_t myTicket = next++;
    while (myTicket != serving.load(std::memory_order_acquire)) {
      Target::Yield();
    }
  }
  void unlock() {
    serving.fetch_add(1, std::memory_order_release);
  }
  char const * name() const {
    return "Ticket";
  }
  static abstractLock * create(uint64_t) {
    return new ticketLock();
  }
};

// A Mellor-Crummey, Scott queueing lock.
class MCSLock : public abstractLock {
  class MCSLockEntry {
  public:
    CACHE_ALIGNED std::atomic<MCSLockEntry *> next;
    std::atomic<bool> go;

    MCSLockEntry() : next(0), go(false) {}
    ~MCSLockEntry() {}
  };

  CACHE_ALIGNED std::atomic<MCSLockEntry *> tail;
  MCSLockEntry entries[LOMP_MAX_THREADS];

public:
  MCSLock() : tail(0) {}
  ~MCSLock() {}
  bool isLocked() const {
    return tail != 0;
  }

  void lock() {
    int myId = omp_get_thread_num();
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
    int myId = omp_get_thread_num();
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
  static abstractLock * create(uint64_t) {
    return new MCSLock();
  }
};

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

  void lock() {
    while (baseLock.isLocked()) {
      // Wait for the base lock to be unlocked.
      // This is unpleasant and makes the use of a queueing lock somewhat
      // futile, since under contention many threads will be polling here.
      Target::Yield();
    }

    // Try to speculate
    if (Target::StartSpeculation() == -1) {
      // Executing speculatively.

      // Now check that the lock was not taken since we checked.  If it has
      // been, we must abort and go to the backoff path, or wait until it
      // isn't, and then try speculation again.  At the moment we do the first
      // of those, and immediately revert to real locking. However, that makes
      // it very hard to recover and start speculating again, so it may be
      // better to wait longer before we give up.
      if (baseLock.isLocked()) {
        // Executing this causes the Target::StartSpeculation
        // call above to return again!
        // This is unpleasant, since it has to be a macro, as the argument
        // is a compiled into the instruction.
        Target_AbortSpeculation(0);
        // Execution cannot get to this point.
      }
      // N.B. executing baseLock.isLocked() above ensures that it is
      // in our read-set, so that if the lock is taken we will abort.
      // We're now inside the transaction and can let the user code run.
      return;
    }
    else {
      // Aborted for some reason
      // is held, take the lock normally.
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
  static abstractLock * create(uint64_t) {
    return new speculativeLock();
  }
};

// Run 50 8MiB copies, collecting BW for each copy.
#define MiB 1024 * 1024
#define SIZE 8 * MiB
static char array1[SIZE];
static char array2[SIZE];

static void runCopies(statistic * s) {
  int const numSamples = 50;
  char * from = &array1[0];
  char * to = &array2[0];

  for (int i = 0; i < numSamples; i++) {
    tsc_tick_count start;
    memcpy(to, from, SIZE);
    tsc_tick_count end;
    double bandwidth = SIZE / (end - start).seconds();
    s->addSample(bandwidth);

    char * tmp = from;
    from = to;
    to = tmp;
  }
}

static void measureInterference(abstractLock * l, statistic * s, int nPollers) {
#pragma omp parallel
  {
    int me = omp_get_thread_num();

    if (me == 0) {
      l->lock();
      // More non-standard, but functional, use of omp barrier...
#pragma omp barrier
      runCopies(s);
      l->unlock();
    }
    else {
#pragma omp barrier
      if (me <= nPollers) {
        l->lock();
        l->unlock();
      }
    }
  }
}

static void runInterference(abstractLock * l, statistic * stats,
                            int maxPollers) {
  // Fill the arrays with random data, then run one set of copies.
  mlfsr32 random;
  for (int i = 0; i < SIZE; i++) {
    array1[i] = char(random.getNext() & 0xff);
    array2[i] = char(random.getNext() & 0xff);
  }
  // Warm up, so that we don't time page instantiation and zeroing.
  runCopies(&stats[0]);

  // Now do the timed runs
  stats[0].reset();
  for (int pollers = 0; pollers <= maxPollers; pollers++) {
    measureInterference(l, &stats[pollers], pollers);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}

#define NUMLOCKS 4096
static void measureOverhead(abstractLock ** l, statistic * summary,
                            int participants) {
  int const numRepeats = 250;
  int const innerReps = 5000;
  statistic threadStats[LOMP_MAX_THREADS];

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    mlfsr32 random;
    statistic * myStats = &threadStats[me];

    if (me < participants) {
      for (int r = 0; r < numRepeats; r++) {
        TIME_BLOCK(myStats);
        for (int i = 0; i < innerReps; i++) {
          abstractLock * theLock = l[random.getNext() % NUMLOCKS];
          theLock->lock();
          theLock->unlock();
        }
      }
    }
  }
  for (int t = 0; t < participants; t++) {
    threadStats[t].scaleDown(innerReps);
    *summary += threadStats[t];
  }
  // Convert to times
  summary->scale(lomp::tsc_tick_count::getTickTime());
}

static void runOverhead(abstractLock ** l, statistic * stats, int maxThreads) {
  for (int t = 1; t <= maxThreads; t++) {
    measureOverhead(l, &stats[t - 1], t);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}
#define MAX_LOCKS 16
static void measureClumpingN(abstractLock ** l, int nLocks, statistic * summary,
                             int participants) {
  int const numRepeats = 25;
  int const innerReps = 1000;
  statistic forkJoin;

  // Measure the number of times each thread reclaims the lock
  // from itself, and express that as a percentage.
  int lastHeld[MAX_LOCKS];
  for (int i = 0; i < MAX_LOCKS; i++)
    lastHeld[i] = -1;
  statistic threadStats[LOMP_MAX_THREADS];

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    statistic * myStat = &threadStats[me];
    for (int r = 0; r < numRepeats; r++) {
      int myCount = 0;

      if (me < participants) {
        for (int i = 0; i < innerReps; i++) {
          int idx = i % nLocks;
          l[idx]->lock();
          if (lastHeld[idx] == me)
            myCount++;
          else
            lastHeld[idx] = me;
          l[idx]->unlock();
        }
      }
#pragma omp barrier
      if (me < participants)
        myStat->addSample((100.0 * myCount) / (innerReps - nLocks));
#pragma omp barrier
#pragma omp master
      {
        // Aggregate the per-thread stats
        for (int i = 0; i < participants; i++) {
          *summary += threadStats[i];
          threadStats[i].reset();
        }
        for (int i = 0; i < MAX_LOCKS; i++)
          lastHeld[i] = -1;
        lastHeld[0] = lastHeld[1] = -1;
      }
    }
  }
}

static void reference(uint32_t value) {
  __asm__ volatile("# Ensure compiler doesn't eliminate the value"
                   :
                   : "r"(value));
}

// Measure the exclusive time when we alternate betwen two different locks
// in an attempt to avoid clumping
static void measureExclusiveN(abstractLock ** l, int nLocks,
                              statistic * summary, int participants) {
  int const numRepeats = 25;
  int const innerReps = 1000;
  statistic forkJoin;

  // Measure the overhead of a parallel region
  for (int i = 0; i < numRepeats; i++) {
    TIME_BLOCK(&forkJoin);
#pragma omp parallel
    ;
  }

  // fprintf(stderr, "\nForkjoin time: %s\n",forkJoin.format('T').c_str());

  // Now measure the induced lock time
  double tickTime = lomp::tsc_tick_count::getTickTime();
  for (int r = 0; r < numRepeats; r++) {
    statistic elapsed;
    elapsed.setOffset(forkJoin.getMean());

    {
      TIME_BLOCK(&elapsed);
#pragma omp parallel
      {
        if (omp_get_thread_num() < participants) {
          for (int i = 0; i < innerReps; i++) {
            abstractLock * lock = l[i % nLocks];
            lock->lock();
            lock->unlock();
          }
        }
      }
    }
    // Convert to a per op time
    elapsed.scale(tickTime / (participants * innerReps));
    *summary += elapsed;
  }
}

typedef void (*testN)(abstractLock **, int, statistic *, int);

static void runTestN(testN op, abstractLock ** l, int nLocks, statistic * stats,
                     int maxThreads) {
  for (int t = 1; t <= maxThreads; t++) {
    op(l, nLocks, &stats[t - 1], t);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}

static void measureMap(abstractLock * l, statistic * s, int nThreads,
                       uint32_t updatePct) {
  std::unordered_map<uint32_t, uint32_t> theMap;
  int const numRepeats = 25;
  int const innerReps = 1000;
  int const entries = 10000;
  statistic threadStats[LOMP_MAX_THREADS];

  for (uint32_t i = 0; i < entries; i++)
    theMap[i] = i * i;

#pragma omp parallel
  {
    int me = omp_get_thread_num();
    mlfsr32 myRandomPos;
    mlfsr32 myRandomSeq(me + 1);

    for (int rep = 0; rep < numRepeats; rep++) {
      if (me < nThreads) {
        TIME_BLOCK(&threadStats[me]);
        for (int i = 0; i < innerReps; i++) {
          uint32_t key = myRandomPos.getNext() % entries;
          // Choose whether to update or not.
          if ((myRandomSeq.getNext() % 100) < updatePct) {
            l->lock();
            theMap[key] += 1;
            l->unlock();
          }
          else {
            l->lock();
            reference(theMap[key]);
            l->unlock();
          }
        }
      }
    }
  }
  // Accumulate statistics
  for (int i = 0; i < nThreads; i++) {
    *s += threadStats[i];
  }
  s->scaleDown(innerReps);
  // Convert to seconds
  s->scale(lomp::tsc_tick_count::getTickTime());
}

//
// Run a sanity check to show that the lock is actually enforcing locking!
//
static void runSanity(abstractLock * l) {
  enum { ITERATIONS = 100000 };
  std::atomic<int32_t> total;
  total = 0;

#pragma omp parallel
  {
    mlfsr32 random;
#pragma omp for
    for (int i = 0; i < ITERATIONS; i++) {
      l->lock();
      int value = total;
      // Wait a random amount of time to give others the chance to interfere
      // if the locks aren't working!
      int sleeps = random.getNext() & 0x1f;
      for (int i = 0; i < sleeps; i++)
        Target::Yield();
      total = value + 1;
      l->unlock();
    }
  }

  printf("%s: %d threads, counted %d which is %s\n", l->name(),
         omp_get_max_threads(), int32_t(total),
         total == ITERATIONS ? "correct" : "***INCORRECT***");
}

static void runMap(abstractLock * l, statistic * stats, int maxThreads,
                   uint32_t updatePct) {
  for (int t = 1; t <= maxThreads; t++) {
    measureMap(l, &stats[t - 1], t, updatePct);
    fprintf(stderr, ".");
  }
  fprintf(stderr, "\n");
}

static std::string getDateTime() {
  auto now = std::time(0);

  return std::ctime(&now);
}

static struct {
  char tag;
  abstractLock * (*creator)(uint64_t);
  uint64_t creationArg;
} lockTable[] = {
    {'A', TASLock::create, 0},
    {'B', ticketLock::create, 0},
    {'C', cxxMutexLock::create, 0},
    {'M', MCSLock::create, 0},
    {'O', openMPLock::create, omp_lock_hint_none},
    {'P', pthreadMutexLock::create, 0},
    {'Q', speculativeLock<TTASLock>::create, 0},
    {'R', speculativeLock<MCSLock>::create, 0},
    {'S', openMPLock::create, omp_lock_hint_speculative},
    {'T', TTASLock::create, 0},
    {'U', TTASLockBO::create, 0},
    {'X', XCHGLock::create, 0},
};

static void printHelp() {
  printf(
      "One argument is always required. The first letter chooses the test "
      "to be "
      "performed,\n"
      "the second the lock type to be measured.\n"
      "For some tests a second argument is also needed. They describe it "
      "below.\n"
      "The first argument is one of\n"
      "C n measure clumping; %% of time a thread reclaims a lock with n locks\n"
      "I   measure interference of waiting threads on a memcpy operation\n"
      "M pct  measure performance of locked access to a std::unordered_map "
      "(with pct percent\n"
      "updates)\n"
      "O   measure overhead (time for acquire/release with no work in "
      "critical section)\n"
      "S   sanity check that the lock enforces exclusivity\n"
      "X n measure exclusive time induced by n locks\n"
      "Y   measure excelusive time switching between two locks\n"
      "... more to come ...\n\n"
      "The second argument is one of\n"
      "A   Test&Set lock\n"
      "B   Bakery (ticket) lock\n"
      "C   C++ std::mutex\n"
      "M   MCS lock\n"
      "O   OpenMP lock with no hint\n"
      "P   pthread_mutex_lock\n"
      "Q   speculative TTAS lock (only on modern X86...)\n"
      "R   speculative MCS lock (only on modern X86...)\n"
      "S   OpenMP lock with omp_lock_hint_speculative\n"
      "T   Test and Test&Set lock\n"
      "U   Test and Test&Set lock with random exponential backoff\n"
      "X   Exchange lock\n");
}

static abstractLock * createLock(char tag) {
  for (int i = 0; i < int(sizeof(lockTable) / sizeof(lockTable[0])); i++) {
    if (lockTable[i].tag == tag)
      return lockTable[i].creator(lockTable[i].creationArg);
  }

  printf("%c is not a valid lock\n", tag);
  printHelp();
  return 0;
}

int main(int argc, char ** argv) {
  int nThreads = omp_get_max_threads();
  if (nThreads > LOMP_MAX_THREADS) {
    printf("%d threads available, increase LOMP_MAX_THREADS (%d)\n", nThreads,
           LOMP_MAX_THREADS);
    return 1;
  }

  if (argc < 2) {
    printf("Need an argument\n");
    printHelp();
    return 1;
  }

  std::string targetName = Target::CPUModelName();
  if (getenv("TARGET_MACHINE"))
    targetName = getenv("TARGET_MACHINE");

    // Warm up...
#pragma omp parallel
  { forceAffinity(); }

  statistic statsValues[LOMP_MAX_THREADS];
  statistic * stats;

  switch (argv[1][0]) {
  case 'S': { // Sanity
    abstractLock * l = createLock(argv[1][1]);
    if (!l)
      return 1;
    runSanity(l);
    break;
  }
  case 'I': { // Interference
    stats = &statsValues[0];
    abstractLock * l = createLock(argv[1][1]);
    if (!l)
      return 1;
    runInterference(l, stats, nThreads - 1);
    printf("Polling Interference\n"
           "%s, %s\n"
           "# %s"
           "%s"
           "# memcpy bandwidth with N pollers running\n"
           "Pollers,  Count,       Min,      Mean,       Max,        SD\n",
           targetName.c_str(), l->name(), getDateTime().c_str(),
           hintsIgnored ? "# BEWARE lock hints ignored\n" : "");
    for (int i = 0; i < nThreads; i++)
      printf("%6d, %s\n", i, stats[i].format(' ').c_str());
    break;
  }
  case 'M': { // Map performance
    if (argc < 3) {
      printf("Need a lock and also an update percentage.\n");
      printHelp();
      return -1;
    }
    uint32_t updatePct = atoi(argv[2]);
    if (updatePct > 100) {
      printf("Cannot give an update percentage greater than 100%%\n");
      return 1;
    }
    stats = &statsValues[0];
    abstractLock * l = createLock(argv[1][1]);
    if (!l)
      return 1;
    runMap(l, stats, nThreads, updatePct);
    printf("std::unordered_map\n"
           "%s, %s, update %u%%\n"
           "# %s"
           "%s"
           "Threads,  Count,       Min,      Mean,       Max,        SD\n",
           targetName.c_str(), l->name(), updatePct, getDateTime().c_str(),
           hintsIgnored ? "# BEWARE lock hints ignored\n" : "");
    for (int i = 0; i < nThreads; i++)
      printf("%6d, %s\n", i + 1, stats[i].format('s').c_str());
    break;
  }
  case 'O': { // Overhead
    stats = &statsValues[0];
    abstractLock * locks[NUMLOCKS];

    locks[0] = createLock(argv[1][1]);
    if (!locks[0])
      return 1;
    for (int i = 1; i < NUMLOCKS; i++)
      locks[i] = createLock(argv[1][1]);
    runOverhead(&locks[0], stats, nThreads);
    printf("Lock Overhead\n"
           "%s, %s\n"
           "# %s"
           "%s"
           "# Time in a thread to execute an empty critical section with N "
           "threads\n"
           "# each picking a random lock from %d\n"
           "Threads,  Count,       Min,      Mean,       Max,        SD\n",
           targetName.c_str(), locks[0]->name(), getDateTime().c_str(),
           hintsIgnored ? "# BEWARE lock hints ignored\n" : "", NUMLOCKS);
    for (int i = 0; i < nThreads; i++)
      printf("%6d, %s\n", i + 1, stats[i].format('s').c_str());
    break;
  }
  case 'C':   // Clumping and exclusive. Syntactically similar
  case 'X': { // Lock exclusive time
    stats = &statsValues[0];
    if (argc < 3) {
      fprintf(stderr, "Need a count for number of locks\n");
      return -1;
    }
    int nLocks = atoi(argv[2]);
    if (nLocks < 1 || nLocks > MAX_LOCKS) {
      fprintf(stderr, "Number of locks shoudl be between 1 and %d\n",
              MAX_LOCKS);
      return -1;
    }
    abstractLock * locks[16];
    locks[0] = createLock(argv[1][1]);
    if (!locks[0])
      return 1;
    for (int i = 1; i < nLocks; i++)
      locks[i] = createLock(argv[1][1]);
    testN func;
    char const * testName;
    char unit;
    if (argv[1][0] == 'C') {
      func = measureClumpingN;
      testName = "Lock Reclaim Rate";
      unit = '%';
    }
    else {
      func = measureExclusiveN;
      testName = "Lock Exclusive Time";
      unit = 's';
    }
    runTestN(func, locks, nLocks, stats, nThreads);
    printf("%s\n"
           "%s, %s, %d locks\n"
           "# %s"
           "%s"
           "Threads,  Count,       Min,      Mean,       Max,        SD\n",
           testName, targetName.c_str(), locks[0]->name(), nLocks,
           getDateTime().c_str(),
           hintsIgnored ? "# BEWARE lock hints ignored\n" : "");
    for (int i = 0; i < nThreads; i++)
      printf("%6d, %s\n", i + 1, stats[i].format(unit).c_str());
    break;
  }
  default:
    printf("Unknown experiment\n");
    printHelp();
    return 1;
  }

  return 0;
}
