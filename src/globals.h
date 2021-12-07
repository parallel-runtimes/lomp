//===-- globals.h - Runtime global declarations  ----------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definitions of runtime global variables and non-class functions.
///
//===----------------------------------------------------------------------===//
#ifndef GLOBALS_H_INCLUDED
#define GLOBALS_H_INCLUDED

namespace lomp {
// Variables
extern bool RuntimeInitialized; /* Has the runtime been initialized? */

extern int NumThreads;

// Functions.
void initializeRuntime();
void intializeLocks();
double getTime();
void displayEnvironment(int verbosity);

} // namespace lomp

#endif
