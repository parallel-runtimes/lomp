//===-- util.cc - Utility functions -----------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdarg>
#include <cstdlib> // for atexit
#include <cstring>
#include <unistd.h> // for STDERR_FILENO, even though we know it's 2

#include "target.h"
#include "debug.h"
#include "event-trace.h"

namespace lomp {

// size of the debug print buffers
enum : size_t { DEBUG_BUFSZ = 8 * 1024 };

static eventTracer * Tracer = nullptr;

static void eprintf(char const * tag, char const * Format, va_list & VarArgs,
                    bool Newline = true) {
  // Buffer to ensure a single write operation (though even that may not be atomic?)
  char buffer[DEBUG_BUFSZ];
  size_t pos = 0;
  if (tag) {
    pos = strlen(tag);
    if (UNLIKELY(pos >= DEBUG_BUFSZ)) {
      fatalError("Prefix in eprintf is too long.");
      return;
    }
    strcpy(&buffer[0], tag);
  }

  pos += vsnprintf(&buffer[pos], DEBUG_BUFSZ - pos, Format, VarArgs);

  if ((pos < (DEBUG_BUFSZ - 1)) && Newline) {
    buffer[pos] = '\n';
    pos += 1;
  }
  // Ensure that we never write from beyond the buffer.
  pos = std::min(pos, size_t(DEBUG_BUFSZ));
  // Try to ensure a single write.
  // Writing to a stream (even stderr) need not be atomic.
  // Since this is for error messages we'd rather ensure we see them than
  // be efficient about how many system calls we make!
  write(STDERR_FILENO, &buffer[0], pos);
}

// Having these error functions here makes it possible to use just this object file
// for stats/timing without requiring the whole runtime.
// Since we want to be able to time other OpenMP runtimes, that is rather useful!
void errPrintf(char const * Format, ...) {
#if (LOMP_BUILD_RTL)
  const char * prefix = "LOMP:";
#else
  const char * prefix = "";
#endif
  va_list VarArgs;
  va_start(VarArgs, Format);
  eprintf(prefix, Format, VarArgs, false);
  va_end(VarArgs);
}

[[noreturn]] void fatalError(char const * Format, ...) {
#if (LOMP_BUILD_RTL)
  const char * prefix = "LOMP:***FATAL ERROR*** ";
#else
  const char * prefix = "***FATAL ERROR*** ";
#endif
  fflush(stdout);
  va_list VarArgs;
  va_start(VarArgs, Format);
  eprintf(prefix, Format, VarArgs);
  va_end(VarArgs);
  if (Tracer) {
    va_start(VarArgs, Format);
    Tracer->insertEvent(Format, VarArgs);
    va_end(VarArgs);
    // No need to output the trace explicitly, since we have an atexit() hook.
  }
  abort();
}

void printWarning(char const * Format, ...) {
#if (LOMP_BUILD_RTL)
  const char * prefix = "LOMP:***WARNING*** ";
#else
  const char * prefix = "***WARNING*** ";
#endif
  va_list VarArgs;
  va_start(VarArgs, Format);
  eprintf(prefix, Format, VarArgs);
  va_end(VarArgs);
}

#if (DEBUG != 0)
/* A function we can hang on atexit(), or call from a debugger, to dump the
 * final parts of the trace if we're tracing.
 */
extern "C" void dumpTrace() {
  Tracer->output();
}

// It's easier to use this than the one in environment.cc because we
// also link this file into the microBM codes which don't need the rest
// of the library.
static int getIntEnv(char const * name) {
  char const * env = getenv(name);
  int result = env ? atoi(env) : 0;

  // errPrintf("GetIntEnv(%s) returns %d\n", name, result);
  return result;
}

static int DebugLevel = -1;

void debug(int level, const char * fmt, ...) {
#if (LOMP_BUILD_RTL)
  const char * debug_suffix = "LOMP:";
#else
  const char * debug_suffix = "DBG:";
#endif

  // Racy, but not a big deal since all threads will agree even if there is a race.
  if (DebugLevel < 0) {
    DebugLevel = getIntEnv("LOMP_DEBUG");
    // If the LOMP_TRACE envirable is set then that sets the debug
    // level but puts the debug message into the limited size log
    // for later output, rather then printing them on the fly.
    // That is potentially faster and less intrusive when looking for
    // things like race conditions...
    int Trace = getIntEnv("LOMP_TRACE");
    if (Trace > 0) {
      Tracer = new eventTracer;
      DebugLevel = Trace;
      // We're unlikely to see this since the tracer is a circular buffer of the
      // last numEvents events, but it doesn't hurt to put it in.
      Tracer->insertEvent("Tracing initialized at debug level %d", DebugLevel);
      atexit(dumpTrace);
    }
  }

  if (level <= DebugLevel) {
    va_list vargs;
    va_start(vargs, fmt);

    if (Tracer) {
      Tracer->insertEvent(fmt, vargs);
    }
    else {
      eprintf(debug_suffix, fmt, vargs);
    }
    va_end(vargs);
  }
}

void debugmsg(int level, const char * msg) {
  lomp::debug(level, "%s", msg);
}

template <typename T>
int debugraw_conv(char * buffer, const char * fmt, int size, T data,
                  int element) {
  return snprintf(buffer, size, fmt, data[element]);
}

void debugraw(int level, const char * msg, int cnt, const char * fmt,
              void * data) {
  if (level > DebugLevel) {
    // we won't print, so bail out of this function, not doing anything
    return;
  }

  const size_t bufsz = DEBUG_BUFSZ;
  char buffer[bufsz];
  int pos = 0;

  pos += snprintf(buffer + pos, bufsz - pos, "%s", msg);
  for (int i = 0; i < cnt; ++i) {
    if (strstr(fmt, "%d")) {
      pos += debugraw_conv(buffer + pos, fmt, bufsz - pos, (int *)data, i);
    }
    else if (strstr(fmt, "%ld")) {
      pos += debugraw_conv(buffer + pos, fmt, bufsz - pos, (long *)data, i);
    }
    else if (strstr(fmt, "%u")) {
      pos += debugraw_conv(buffer + pos, fmt, bufsz - pos, (unsigned int *)data,
                           i);
    }
    else if (strstr(fmt, "%lu")) {
      pos += debugraw_conv(buffer + pos, fmt, bufsz - pos,
                           (unsigned long *)data, i);
    }
    else {
      fatalError("Unknown format string in format for debugraw()");
    }
  }

  debugmsg(level, buffer);
}

#endif

} // namespace lomp
