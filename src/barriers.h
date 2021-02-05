//===-- barriers.h - Implementation of fork/join and barriers ---*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the interface to barriers and fork/join.
/// The interface is abstracted here as a base class, specific implementations are in the
/// barriers.cc file.
//===----------------------------------------------------------------------===//
#ifndef BARRIERS_H_INCLUDED
#define BARRIERS_H_INCLUDED

#include <cstdarg>
#include <string>

#include "target.h"
#include "thunk.h"

namespace lomp {

// Packaged up invocation information to allow a single thread to apply the outlined body
// with the appropriate arguments.
class InvocationInfo {
  enum InvocationType {
    CallLLVM,
#if (LOMP_GNU_SUPPORT)
    CallGNU
#endif
  };
  InvocationType Type;
  union {
    BodyTypeLLVM BodyLLVM;
#if (LOMP_GNU_SUPPORT)
    BodyTypeGNU BodyGNU;
#endif
  };
  int ArgCount;
  union {
    va_list * ArgsLLVM;
#if (LOMP_GNU_SUPPORT)
    void * ArgsGNU;
#endif
  };

public:
  InvocationInfo() {}
  InvocationInfo(BodyTypeLLVM B, int AC, va_list * A)
      : Type(CallLLVM), BodyLLVM(B), ArgCount(AC), ArgsLLVM(A) {}
#if (LOMP_GNU_SUPPORT)
  InvocationInfo(BodyTypeGNU B, void * A)
      : Type(CallGNU), BodyGNU(B), ArgCount(1), ArgsGNU(A) {}
#endif
  void run(void * GTid, void * LTid) const;
  void runLLVM(void * GTid, void * LTid) const;
#if (LOMP_GNU_SUPPORT)
  void runGNU(void * GTid, void * LTid) const;
#endif
};

/// An abstract interface; there are many possible implementations...
/// At present this assumes a centralized implementation, that should not be a
/// general requirement, though it is useful when we need to separate the checkIn and checkOut
/// operations to implement join and fork.
/// We also need to consider how reduction interacts with barriers, and implement that too.

// TODO: add reduction support.
class Barrier {
  // No data members. This is just defining an interface.
public:
  // These could be in a namespace, but it really makes little difference.
  typedef Barrier * (*barrierFactory)(int);
  struct barrierDescription {
    char const * name;
    barrierFactory factory;
    char const * (*getFullName)();
  };
  static barrierDescription * findBarrier(std::string const & name);
  // So that code can iterate over all barriers without knowing what they are.
  static barrierDescription * getBarrier(int n);
  static void printBarriers();

  // Now we have real class members.
  static Barrier * newBarrier(int NumThreads);
  // A static one so that we can get the name before we construct the object!
  static char const * barrierName();

  Barrier() {}
  virtual ~Barrier() {}

  // Default full barrier assumes a fork/join, but this can be overridden.
  virtual void fullBarrier(int me) {
    bool root = checkIn(me, true);
    checkOut(root, me);
  }

  // Override this if the barrier is distributed, rather than centralizing.
  // This is mostly useful for benchmarking code, which may want to measure
  // checkIn and checkOut separately if that makes sense.
  virtual bool isDistributed() const {
    return false;
  }

  // This non-virtual function includes the interface to the tasking
  // system, and then invokes the underlying barrier code (through the
  // virtual function) which, as a result, doesn't need to worry about that.
  // That separates the tasking code (which is needed inside the OpenMP
  // runtime) from the barrier code which can be useful elsewhere.
  // (At least, in microbenchmarks!)
  bool checkIn(int me, bool internalBarrier);

  // Here are the methods the barrier implementation must provide.  It
  // can also override the fullBarrier method, but that only makes
  // sense if it is a single phase, non-centralizing, barrier, which
  // can only reasonably used in a few places, since in the OPenMP
  // runtime context we normally use the checkOut for fork and checkIn
  // for join around our parallel regions.

  // All threads check in
  virtual bool checkIn(int me) = 0;
  // Thread zero can either explicitly wake everyone up and pass out invocation info,
  // or checkOut like everyone else (in which case no invocation info will be passed).
  virtual void wakeUp(int me, InvocationInfo const * Args) = 0;
  // Threads other than thread zero wait at checkOut to be woken.
  virtual InvocationInfo const * checkOut(bool root, int me) = 0;
  virtual char const * name() const = 0;
};

} // namespace lomp

#endif
