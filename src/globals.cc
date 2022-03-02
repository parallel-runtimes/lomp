//===-- globals.cc - Runtime global declarations ----------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of runtime global non-class functions, and the
/// instantiaition of global variables.
///
//===----------------------------------------------------------------------===//

#include <utility>
#include <chrono>

#include "globals.h"
#include "threads.h"
#include "environment.h"
#include "locks.h"
#include "numa_support.h"
#include "mlfsr32.h"

#include "version.h"

namespace lomp {

bool RuntimeInitialized = false;

// list of ICVs supported
const int DefaultNumThreads = 0; // Let the ThreadTeam choose.
int NumThreads;                  // OMP_NUM_THREADS

// Display the LOMP environment
void displayEnvironmentVariable(const std::string & name) {
  std::string value;
  environment::getString(name.c_str(), value, "");
  if (value.empty()) {
    printf("  [host] %s: value is not defined\n", name.c_str());
  }
  else {
    printf("  [host] %s='%s'\n", name.c_str(), value.c_str());
  }
}

void displayEnvironment(displayVerbosity verbosity) {
  auto standardDisplay = {"OMP_NUM_THREADS","OMP_SCHEDULE", "OMP_DISPLAY_ENV"};
  auto verboseDisplay = {"LOMP_LOCK_KIND", "LOMP_BARRIER_KIND", "LOMP_DEBUG", "LOMP_TRACE"};
  printf("OPENMP DISPLAY ENVIRONMENT\n");
  printf("  _OPENMP='%d'\n", 0);
  for (auto var : standardDisplay) {
    displayEnvironmentVariable(var);
  }
  if (verbosity == displayVerbosity::verbose) {
    for (auto var : verboseDisplay) {
      displayEnvironmentVariable(var);
    }
  }
  printf("OPENMP DISPLAY ENVIRONMENT END\n");
}

// Set up necessary runtime data structures.
void initializeRuntime() {
  debug(Debug::Announce,
        "runtime version " LOMP_VERSION " (SO version " LOMP_SOVERSION ")"
        " compiled at " __TIME__ " on " __DATE__);
  debug(Debug::Announce, "from Git commit " LOMP_GIT_COMMIT_ID
                         " for " LOMP_TARGET_ARCH_NAME " by " COMPILER_NAME);
  debug(Debug::Announce, "with configuration " LOMP_COMPILE_OPTIONS
                         ";" LOMP_COMPILE_DEFINITIONS);

  numa::InitializeNumaSupport();
  if (NumThreads == 0) {
    // Only set it from the environment if it hasn't already been forced
    // by omp_set_num_threads().
    environment::getInt("OMP_NUM_THREADS", NumThreads);
    if (NumThreads < 1) {
      NumThreads = DefaultNumThreads;
    }
  }

  // Parse the envirable if it is set.
  Thread::initializeForcedReduction();

  auto * team = new ThreadTeam(NumThreads);
  // Refer to it even when debug is macro-ed out to
  // avoid a compiler warning.
  (void)team;
  debug(Debug::Info, "Using %d threads", team->getCount());

  // Let the lock subsystem parse envirables to choose the lock type.
  lomp::locks::InitializeLocks();
  // and the loop scheduling parse OMP_SCHEDULE so that schedule(runtime)
  // works.
  initializeLoops();

  // Print LOMP environment if requested via OMP_DISPLAY_ENV
  std::string env;
  auto verbosity = displayVerbosity::disabled;
  environment::getString("OMP_DISPLAY_ENV", env, "false");
  if (env == "true" || env == "1") {
    verbosity = displayVerbosity::enabled;
  }
  else if (env == "verbose") {
    verbosity = displayVerbosity::verbose;
  }
  if (verbosity != displayVerbosity::disabled) {
    displayEnvironment(verbosity);
  }

  RuntimeInitialized = true;
}

double getTime() {
  using clock_t = std::chrono::high_resolution_clock;
  using unit_t = std::chrono::microseconds;
  const auto now = clock_t::now();
  const auto time =
      std::chrono::duration_cast<unit_t>(now.time_since_epoch()).count();
  return static_cast<double>(time) / 1000000.0;
}

} // namespace lomp
