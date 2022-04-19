/** @file debug.h
 * Debug macros
 */

//===----------------------------------------------------------------------===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED

#include "globals.h"

/// Checks \p cond and aborts if it is not true.
#define LOMP_ASSERT(cond)                                                      \
  do {                                                                         \
    if (UNLIKELY(!(cond))) {                                                   \
      lomp::fatalError("ASSERTION: \"" STRINGIFY(                              \
          cond) "\" failed at " __FILE__ ":" STRINGIFY(__LINE__) " ***");      \
    }                                                                          \
  } while (0)

namespace lomp {
/// Output a printf formatted string to stderr.
void errPrintf(const char * Format, ...);
/// Output a printf formatted warning to stderr, but then continue.
void printWarning(const char * Format, ...);
/// Output a printf formatted string to stderr and then exit.
[[noreturn]] void fatalError(const char * Format, ...);

// Debug levels
enum Debug {
  Always = -2,
  Announce =
      1, /* Just the announcement that this runtime is running and its version */
  Info = 2, /* NUMA info and so on. */
  Detailed = 10,
  // Switch around when debugging specific subsystems so that you can get info from only the subsystem you want.
  Loops = 15,
  Reduction = 20,
  Threads = 30,
  MemoryAllocation = 40,
  Barriers = 50,
  Locks = 60,
  Functions = 1000,
};

#if (DEBUG > 0)
/// Outputs a printf style message to stderr if \p level is greater or equal
/// to that requested globally.
void debug(int level, const char * fmt, ...);

/// Outputs a single string to stderr if \p level is greater or equal
/// to that requested globally.
void debugmsg(int level, const char * msg);

/// Output a fixed number of elements if \p level is greater or equal
/// to that requested globally.
void debugraw(int level, const char * msg, int cnt, const char * fmt,
              void * data);

/// Flush the trace buffer to stderr. extern "C" so that it's easy to call from
/// a debugger.
extern "C" void dumpTrace();

/// Traces entry to a function if the debug level is >= lomp::DebugFunctions
#define debug_enter()                                                          \
  do {                                                                         \
    lomp::debug(lomp::Debug::Functions, "entering %s at %s:%d", __FUNCTION__,  \
                __FILE__, __LINE__);                                           \
  } while (0)

/// Traces exit from a function if the debug level is >= lomp::DebugFunctions
#define debug_leave()                                                          \
  do {                                                                         \
    lomp::debug(lomp::Debug::Functions, "leaving %s at %s:%d", __FUNCTION__,   \
                __FILE__, __LINE__);                                           \
  } while (0)

#define DEBUG_ASSERT(cond) LOMP_ASSERT((cond))
// #warning DEBUG code compiled in
#else
// DEBUG code is not required, so eliminate it (while still eating the trailing semi-colon).
// #warning DEBUG code NOT compiled in
#define debug(level, fmt, ...) void(0)
#define debugmsg(level, msg) void(0)
#define debugraw(level, msg, cnt, fmt, data) void(0)
#define dumpTrace() void(0)
#define debug_enter() void(0)
#define debug_leave() void(0)
#define DEBUG_ASSERT(cond) void(0)
#endif

} // namespace lomp

#endif // DEBUG_H_INCLUDED
