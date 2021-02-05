//===-- atomics_impl.h - Implement atomic operation -------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains mostly macro trickery to implement atomic operations.
/// Some of it could potentialy be templates instead, but that seems to get harder than
/// this.
/// Where feasible we use std::atomic directly, where that is not
/// possible (because the operation is not supported in C++11), we
/// explicitly use compare-and-swap implementations.

#include <cstdint>
#include <atomic>
#define UNUSED __attribute__((unused))

#define ExpandBuiltInAtomic(type, name, op)                                    \
  static void atomic##name(void * target, type operand) UNUSED;                \
  static void atomic##name(void * target, type operand) {                      \
    std::atomic<type> * t = (std::atomic<type> *)target;                       \
    *t op## = operand;                                                         \
  }

// Note: this has the minimal back-off which is sane: one pause/yield instruction.
// This could potentially be improved for cases of significant contention.
// We could also consider whether it is better to go directly to the LL-SC
// operations on architectures (such as ARM) which have them, rather than
// having the compiler use them to emulate the cmpxchg.
//
// Note that the load of the value (current.uintValue = *t) seems
// redundant after the first time around the loop, since the
// compare_exchange_weak will have loaded the value. However, that was
// the value before we yielded, and, if we're looping, we know there
// is contention so the value may be changing.  We therefore want the
// speculative region not to include the yield, so we reload.
//
// We should also consider whether to use a "load with write intent" (LWI)
// since otherwise under contention we move the line into a shared state
// and then have to get it exclusive.
// (Although architectures may not explicitly have an LWI instruction,
// using a compare_exchange for the load is normally sufficient!)
//
#define ExpandCASAtomic(type, width, name, op, reversed)                       \
  static void atomic##name(type * target, type operand) UNUSED;                \
  static void atomic##name(type * target, type operand) {                      \
    std::atomic<uint##width##_t> * t = (std::atomic<uint##width##_t> *)target; \
    typedef union {                                                            \
      uint##width##_t uintValue;                                               \
      type typeValue;                                                          \
    } sharedBits;                                                              \
    sharedBits current;                                                        \
                                                                               \
    for (;;) {                                                                 \
      current.uintValue = *t;                                                  \
      sharedBits next;                                                         \
      if (reversed)                                                            \
        next.typeValue = operand op current.typeValue;                         \
      else                                                                     \
        next.typeValue = current.typeValue op operand;                         \
      if (t->compare_exchange_weak(current.uintValue, next.uintValue))         \
        return;                                                                \
      Target::Yield();                                                         \
    }                                                                          \
  }

#define ForeachIntegerOp(macro, type)                                          \
  macro(type, Plus, +) macro(type, Minus, -) macro(type, Bitand, &)            \
      macro(type, BitOr, |) macro(type, BitXor, ^)

ForeachIntegerOp(ExpandBuiltInAtomic, uint8_t)
    ForeachIntegerOp(ExpandBuiltInAtomic, int8_t)
        ForeachIntegerOp(ExpandBuiltInAtomic, uint16_t)
            ForeachIntegerOp(ExpandBuiltInAtomic, int16_t)
                ForeachIntegerOp(ExpandBuiltInAtomic, uint32_t)
                    ForeachIntegerOp(ExpandBuiltInAtomic, int32_t)
                        ForeachIntegerOp(ExpandBuiltInAtomic, uint64_t)
                            ForeachIntegerOp(ExpandBuiltInAtomic, int64_t)

#define ForEachFPOp(macro, type, width)                                        \
  macro(type, width, Plus, +, false) macro(type, width, Minus, -, false)       \
      macro(type, width, ReversedMinus, -, true)                               \
          macro(type, width, Times, *, false)                                  \
              macro(type, width, Divide, /, false)                             \
                  macro(type, width, ReversedDivide, /, true)

                                ForEachFPOp(ExpandCASAtomic, float, 32)
                                    ForEachFPOp(ExpandCASAtomic, double, 64)
