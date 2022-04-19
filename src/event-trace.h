//===-- event_trace.h - Trace events in a multi-threaded code ---*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains an event tracer which should be useful for debugging.

#ifndef EVENT_TRACE_H
#define EVENT_TRACE_H
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "stats-timing.h"

class eventTracer {
#if (DEBUG)
  enum { numEvents = 1024 };
  class event {
    enum { numArgs = 10 };
    CACHE_ALIGNED char const * fmt;
    uintptr_t args[numArgs];
    // Somewhat tacky. We're assuming a fixed number of scalar arguments.
    // We can't trivially use a va_list, since it doesn't copy all of
    // the arguments, but rather provides a way to access them from where they may be resident in the callers stack.
    // Since we're not going to format them until later (or, maybe, not at all), they will have been overwritten
    // before we looked at them.
    // This will not work correctly for pointers to non-static strings...

  public:
    event() : fmt(0) {}
    ~event() {}

    bool inUse() const {
      return fmt != 0;
    }
    void release() {
      fmt = 0;
    }
    // This is the thing we want to be fast.
    void updateEvent(char const * f, va_list & vl) {
      fmt = f;
      for (int i = 0; i < numArgs; i++)
        args[i] = va_arg(vl, uintptr_t);
    }

    // This can be slow; it happens when we're doing I/O anyway.
    std::string format() {
      char buffer[512];

      sprintf(&buffer[0], fmt, args[0], args[1], args[2], args[3], args[4],
              args[5], args[6], args[7], args[8], args[9]);

      return std::string(&buffer[0]);
    }
  };

  CACHE_ALIGNED std::atomic<uint32_t> nextEvent;
  std::atomic<bool> locked;
  event events[numEvents];

public:
  eventTracer() : nextEvent(0), locked(false) {}
  ~eventTracer() {}

  void reset() {
    for (int i=0; i<numEvents; i++)
      events[i].release();
  }
  
  void insertEvent(char const * f, ...) {
    va_list args;
    va_start(args, f);
    insertEvent(f, args);
    va_end(args);
  }

  void insertEvent(char const * f, va_list & args) {
    // If the log is being output wait until that has finished.
    while (locked)
      ;

    int idx = (nextEvent++) % numEvents;
    event * e = &events[idx];
    e->updateEvent(f, args);
  }

  void output(FILE * f = 0) {
    if (!f) {
      f = stderr;
    }

    if (locked.exchange(true)) {
      // Someone else is already printing, so we wait until they have
      // finished (mostly because we might call exit before they're done).
      while (locked)
        ;
      return;
    }
    fprintf(f, "***LOMP Trace***\n");
    for (int i = 0; i < numEvents; i++) {
      int idx = (nextEvent + i) % numEvents;

      event * e = &events[idx];
      if (!e->inUse())
        continue;
      fprintf(f, "%s\n", e->format().c_str());
      e->release();
    }
    locked = false;
  }
#else
public:
  eventTracer() {}
  ~eventTracer() {}
  void insertEvent(char const *, ...) {}
  void insertEvent(char const *, va_list &) {}
  void output(FILE * f = 0) {
    (void)f;
  }
#endif
};
#endif
