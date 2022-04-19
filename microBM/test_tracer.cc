//===-- test_tracer.cc - Test the event tracer -------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains simple code to test the event tracer.

#include <omp.h>
#include <cstdio>
#define DEBUG 1
#include "event-trace.h"

int main(int, char **) {
  eventTracer tracer;

  for (int i = 0; i < 10; i++) {
    tracer.insertEvent(0, "Event %d\n", i);
  }

  tracer.output(stderr);
  return 0;
}
